#!/usr/bin/env python3
"""Operate the local Home Assistant/Zigbee2MQTT test installation.

Authentication can use either a Home Assistant access token or the local
username/password login flow. Prefer environment variables so credentials do
not end up in shell history:

  export HA_URL=http://192.168.1.100:8123
  export HA_USERNAME=lolren
  export HA_PASSWORD=lolren
  python3 scripts/home_assistant_zigbee2mqtt.py status
"""

from __future__ import annotations

import argparse
import getpass
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from typing import Any


class HomeAssistantClient:
    def __init__(
        self,
        base_url: str,
        token: str | None = None,
        username: str | None = None,
        password: str | None = None,
    ) -> None:
        self.base_url = base_url.rstrip("/")
        self.token = token
        self.username = username
        self.password = password

    def _open(self, request: urllib.request.Request) -> Any:
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                if response.status == 204:
                    return None
                return json.load(response)
        except urllib.error.HTTPError as error:
            payload = error.read().decode("utf-8", errors="replace")
            raise RuntimeError(
                f"Home Assistant request failed: HTTP {error.code}: {payload}"
            ) from error
        except urllib.error.URLError as error:
            raise RuntimeError(f"Cannot reach Home Assistant: {error}") from error

    def authenticate(self) -> None:
        if self.token:
            return
        if not self.username:
            raise RuntimeError("Set HA_USERNAME or pass --username")
        if self.password is None:
            self.password = getpass.getpass("Home Assistant password: ")

        client_id = self.base_url + "/"
        redirect_uri = self.base_url + "/?auth_callback=1"
        start_request = urllib.request.Request(
            self.base_url + "/auth/login_flow",
            data=json.dumps(
                {
                    "client_id": client_id,
                    "redirect_uri": redirect_uri,
                    "handler": ["homeassistant", None],
                }
            ).encode("utf-8"),
            headers={"Content-Type": "application/json"},
        )
        start = self._open(start_request)

        flow_request = urllib.request.Request(
            self.base_url + f"/auth/login_flow/{start['flow_id']}",
            data=json.dumps(
                {
                    "username": self.username,
                    "password": self.password,
                    "client_id": client_id,
                }
            ).encode("utf-8"),
            headers={"Content-Type": "application/json"},
        )
        result = self._open(flow_request)
        authorization_code = result.get("result")
        if not authorization_code:
            raise RuntimeError(
                "Home Assistant login did not return an authorization code: "
                + json.dumps(result, sort_keys=True)
            )

        token_request = urllib.request.Request(
            self.base_url + "/auth/token",
            data=urllib.parse.urlencode(
                {
                    "grant_type": "authorization_code",
                    "code": authorization_code,
                    "client_id": client_id,
                }
            ).encode("ascii"),
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )
        self.token = self._open(token_request)["access_token"]

    def request(
        self, path: str, *, method: str = "GET", data: Any | None = None
    ) -> Any:
        self.authenticate()
        body = None
        headers = {"Authorization": f"Bearer {self.token}"}
        if data is not None:
            body = json.dumps(data).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(
            self.base_url + path,
            data=body,
            headers=headers,
            method=method,
        )
        return self._open(request)

    def states(self) -> list[dict[str, Any]]:
        return self.request("/api/states")

    def call_service(self, domain: str, service: str, data: dict[str, Any]) -> Any:
        return self.request(
            f"/api/services/{domain}/{service}", method="POST", data=data
        )

    def mqtt_publish(self, topic: str, payload: str, retain: bool = False) -> Any:
        return self.call_service(
            "mqtt",
            "publish",
            {"topic": topic, "payload": payload, "retain": retain},
        )


def zigbee_entities(states: list[dict[str, Any]]) -> list[dict[str, Any]]:
    matches = []
    needles = ("zigbee", "z2m", "mqtt")
    for state in states:
        blob = json.dumps(state, sort_keys=True).lower()
        if any(needle in blob for needle in needles):
            matches.append(state)
    return matches


def print_json(value: Any) -> None:
    print(json.dumps(value, indent=2, sort_keys=True))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Home Assistant and Zigbee2MQTT local validation helper."
    )
    parser.add_argument(
        "--url",
        default=os.environ.get("HA_URL", "http://192.168.1.100:8123"),
    )
    parser.add_argument("--token", default=os.environ.get("HA_TOKEN"))
    parser.add_argument("--username", default=os.environ.get("HA_USERNAME"))
    parser.add_argument("--password", default=os.environ.get("HA_PASSWORD"))

    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status", help="Check the API and list Zigbee entities.")
    commands.add_parser("entities", help="Print Zigbee/MQTT-related HA states.")

    state = commands.add_parser("state", help="Print one Home Assistant state.")
    state.add_argument("entity_id")

    permit = commands.add_parser(
        "permit-join", help="Open Zigbee2MQTT joining through HA MQTT."
    )
    permit.add_argument("--seconds", type=int, default=180)

    commands.add_parser("close-join", help="Close Zigbee2MQTT joining.")

    interview = commands.add_parser(
        "interview", help="Request a Zigbee2MQTT device interview."
    )
    interview.add_argument("device")

    remove = commands.add_parser(
        "remove", help="Remove a Zigbee2MQTT device from the coordinator."
    )
    remove.add_argument("device")
    remove.add_argument("--force", action="store_true")

    publish = commands.add_parser("publish", help="Publish an MQTT message via HA.")
    publish.add_argument("topic")
    publish.add_argument("payload")
    publish.add_argument("--retain", action="store_true")

    service = commands.add_parser("service", help="Call a Home Assistant service.")
    service.add_argument("domain")
    service.add_argument("service")
    service.add_argument("--data", default="{}")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    client = HomeAssistantClient(
        args.url,
        token=args.token,
        username=args.username,
        password=args.password,
    )

    if args.command == "status":
        api_status = client.request("/api/")
        entities = zigbee_entities(client.states())
        print_json(
            {
                "api": api_status,
                "url": args.url,
                "zigbee_entity_count": len(entities),
                "zigbee_entities": [state["entity_id"] for state in entities],
            }
        )
    elif args.command == "entities":
        print_json(zigbee_entities(client.states()))
    elif args.command == "state":
        print_json(client.request(f"/api/states/{args.entity_id}"))
    elif args.command == "permit-join":
        seconds = max(1, min(args.seconds, 254))
        print_json(
            client.mqtt_publish(
                "zigbee2mqtt/bridge/request/permit_join",
                json.dumps({"value": True, "time": seconds}),
            )
        )
    elif args.command == "close-join":
        print_json(
            client.mqtt_publish(
                "zigbee2mqtt/bridge/request/permit_join",
                json.dumps({"value": False, "time": 0}),
            )
        )
    elif args.command == "interview":
        print_json(
            client.mqtt_publish(
                "zigbee2mqtt/bridge/request/device/interview",
                json.dumps({"id": args.device}),
            )
        )
    elif args.command == "remove":
        print_json(
            client.mqtt_publish(
                "zigbee2mqtt/bridge/request/device/remove",
                json.dumps({"id": args.device, "force": args.force}),
            )
        )
    elif args.command == "publish":
        print_json(client.mqtt_publish(args.topic, args.payload, args.retain))
    elif args.command == "service":
        print_json(
            client.call_service(
                args.domain, args.service, json.loads(args.data)
            )
        )
    else:
        raise AssertionError(f"Unhandled command: {args.command}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
