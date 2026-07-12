package com.lolren.sensevoiceimu

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID
import kotlin.math.sqrt

object StreamProtocol {
    const val VERSION = 1
    const val STREAM_AUDIO = 0x01
    const val STREAM_IMU = 0x02
    const val AUDIO_PACKET_BYTES = 174
    const val AUDIO_FRAME_SAMPLES = 320
    const val AUDIO_SAMPLE_RATE = 16_000
    const val IMU_PACKET_BYTES = 20

    val SERVICE: UUID = UUID.fromString("5f24c001-7e2b-4b8c-ae59-2d7618b9d1a0")
    val CAPABILITIES: UUID = UUID.fromString("5f24c002-7e2b-4b8c-ae59-2d7618b9d1a0")
    val CONTROL: UUID = UUID.fromString("5f24c003-7e2b-4b8c-ae59-2d7618b9d1a0")
    val STATUS: UUID = UUID.fromString("5f24c004-7e2b-4b8c-ae59-2d7618b9d1a0")
    val AUDIO: UUID = UUID.fromString("5f24c005-7e2b-4b8c-ae59-2d7618b9d1a0")
    val IMU: UUID = UUID.fromString("5f24c006-7e2b-4b8c-ae59-2d7618b9d1a0")
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    private val stepTable = intArrayOf(
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
        143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
        494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
        1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
        4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
        11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
        27086, 29794, 32767,
    )
    private val indexTable = intArrayOf(
        -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
    )

    data class AudioFrame(
        val sequence: Int,
        val sampleCounter: Long,
        val discontinuity: Boolean,
        val samples: ShortArray,
        val levelPercent: Int,
    )

    data class ImuFrame(
        val sequence: Int,
        val sampleCounter: Long,
        val ax: Float,
        val ay: Float,
        val az: Float,
        val gx: Float,
        val gy: Float,
        val gz: Float,
    )

    data class Status(
        val state: Int,
        val transaction: Int,
        val audioDrops: Int,
        val imuDrops: Int,
        val requestedStreams: Int,
        val error: Int,
    )

    fun control(streamMask: Int, transaction: Int): ByteArray =
        byteArrayOf(
            VERSION.toByte(),
            streamMask.toByte(),
            transaction.toByte(),
            (transaction ushr 8).toByte(),
        )

    fun decodeAudio(packet: ByteArray): AudioFrame? {
        if (packet.size != AUDIO_PACKET_BYTES || packet[0].toInt() and 0xff != VERSION) {
            return null
        }
        val buffer = ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN)
        val flags = packet[1].toInt() and 0xff
        val sequence = buffer.getShort(2).toInt() and 0xffff
        val sampleCounter = buffer.getInt(4).toLong() and 0xffffffffL
        val sampleCount = buffer.getShort(8).toInt() and 0xffff
        if (sampleCount != AUDIO_FRAME_SAMPLES) return null

        var predictor = buffer.getShort(10).toInt()
        var stepIndex = packet[12].toInt() and 0xff
        if (stepIndex !in 0..88) return null
        val output = ShortArray(sampleCount)
        output[0] = predictor.toShort()
        var squareSum = predictor.toLong() * predictor

        for (sampleIndex in 1 until sampleCount) {
            val nibbleIndex = sampleIndex - 1
            val encoded = packet[14 + nibbleIndex / 2].toInt() and 0xff
            val nibble = if (nibbleIndex and 1 == 0) encoded and 0x0f else encoded ushr 4
            val step = stepTable[stepIndex]
            var delta = step ushr 3
            if (nibble and 4 != 0) delta += step
            if (nibble and 2 != 0) delta += step ushr 1
            if (nibble and 1 != 0) delta += step ushr 2
            predictor += if (nibble and 8 != 0) -delta else delta
            predictor = predictor.coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt())
            stepIndex = (stepIndex + indexTable[nibble]).coerceIn(0, 88)
            output[sampleIndex] = predictor.toShort()
            squareSum += predictor.toLong() * predictor
        }
        val rms = sqrt(squareSum.toDouble() / sampleCount)
        val level = ((rms / 8000.0) * 100.0).toInt().coerceIn(0, 100)
        return AudioFrame(sequence, sampleCounter, flags and 1 != 0, output, level)
    }

    fun decodeImu(packet: ByteArray): ImuFrame? {
        if (packet.size != IMU_PACKET_BYTES || packet[0].toInt() and 0xff != VERSION) {
            return null
        }
        val buffer = ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN)
        val sequence = buffer.getShort(2).toInt() and 0xffff
        val sampleCounter = buffer.getInt(4).toLong() and 0xffffffffL
        fun raw(offset: Int) = buffer.getShort(offset).toInt()
        return ImuFrame(
            sequence,
            sampleCounter,
            raw(8) * 0.000061f,
            raw(10) * 0.000061f,
            raw(12) * 0.000061f,
            raw(14) * 0.00875f,
            raw(16) * 0.00875f,
            raw(18) * 0.00875f,
        )
    }

    fun decodeStatus(packet: ByteArray): Status? {
        if (packet.size != 12 || packet[0].toInt() and 0xff != VERSION) return null
        val buffer = ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN)
        return Status(
            packet[1].toInt() and 0xff,
            buffer.getShort(2).toInt() and 0xffff,
            buffer.getShort(4).toInt() and 0xffff,
            buffer.getShort(6).toInt() and 0xffff,
            packet[8].toInt() and 0xff,
            packet[10].toInt() and 0xff,
        )
    }
}
