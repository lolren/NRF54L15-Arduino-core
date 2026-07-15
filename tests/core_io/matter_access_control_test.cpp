#include <assert.h>
#include <stdint.h>

#include "matter_access_control.h"

using xiao_nrf54l15::AclEntry;
using xiao_nrf54l15::AclPrivilege;
using xiao_nrf54l15::MatterAccessControl;

int main() {
  const uint8_t fabric[8] = {0x10, 1, 2, 3, 4, 5, 6, 7};
  const uint8_t node[8] = {0x20, 1, 2, 3, 4, 5, 6, 7};
  const uint8_t subject[8] = {0x00, 1, 2, 3, 4, 5, 6, 7};
  const uint8_t otherSubject[8] = {0x00, 1, 2, 3, 4, 5, 6, 8};
  const uint8_t zeroSubject[8] = {0};

  MatterAccessControl defaults;
  assert(defaults.addDefaultViewEntry());
  assert(defaults.checkAccess(subject, fabric, node, 0x0006, 1,
                              AclPrivilege::kView));
  assert(!defaults.checkAccess(subject, fabric, node, 0x0006, 1,
                               AclPrivilege::kOperate));

  MatterAccessControl specific;
  AclEntry specificEntry = {};
  for (size_t i = 0; i < sizeof(subject); ++i) {
    specificEntry.subjectId[i] = subject[i];
  }
  specificEntry.wildcardFabric = true;
  specificEntry.wildcardNode = true;
  specificEntry.wildcardCluster = true;
  specificEntry.wildcardEndpoint = true;
  specificEntry.privilege = AclPrivilege::kOperate;
  specificEntry.valid = true;
  assert(specific.addEntry(specificEntry));
  assert(specific.checkAccess(subject, fabric, node, 0x0006, 1,
                              AclPrivilege::kOperate));
  assert(!specific.checkAccess(otherSubject, fabric, node, 0x0006, 1,
                               AclPrivilege::kOperate));

  MatterAccessControl exactZero;
  AclEntry zeroEntry = specificEntry;
  for (uint8_t& value : zeroEntry.subjectId) {
    value = 0;
  }
  assert(exactZero.addEntry(zeroEntry));
  assert(exactZero.checkAccess(zeroSubject, fabric, node, 0x0006, 1,
                               AclPrivilege::kOperate));
  assert(!exactZero.checkAccess(subject, fabric, node, 0x0006, 1,
                                AclPrivilege::kOperate));

  MatterAccessControl nodeOperator;
  assert(nodeOperator.addNodeOperateEntry(node, fabric));
  assert(nodeOperator.checkAccess(otherSubject, fabric, node, 0x0006, 1,
                                  AclPrivilege::kOperate));
  assert(!nodeOperator.checkAccess(nullptr, fabric, node, 0x0006, 1,
                                   AclPrivilege::kOperate));
  assert(!nodeOperator.checkAccess(otherSubject, nullptr, node, 0x0006, 1,
                                   AclPrivilege::kOperate));
  assert(!nodeOperator.checkAccess(otherSubject, fabric, nullptr, 0x0006, 1,
                                   AclPrivilege::kOperate));

  MatterAccessControl extendedCluster;
  AclEntry extendedEntry = specificEntry;
  extendedEntry.clusterId = 0xFFF10006UL;
  extendedEntry.wildcardCluster = false;
  assert(extendedCluster.addEntry(extendedEntry));
  assert(extendedCluster.checkAccess(subject, fabric, node, 0xFFF10006UL, 1,
                                     AclPrivilege::kOperate));
  assert(!extendedCluster.checkAccess(subject, fabric, node, 0x00000006UL, 1,
                                      AclPrivilege::kOperate));

  MatterAccessControl invalidPrivilege;
  AclEntry invalidEntry = specificEntry;
  invalidEntry.privilege = static_cast<AclPrivilege>(0xFFU);
  assert(invalidPrivilege.addEntry(invalidEntry));
  assert(!invalidPrivilege.checkAccess(subject, fabric, node, 0x0006, 1,
                                       AclPrivilege::kView));
  assert(!specific.checkAccess(subject, fabric, node, 0x0006, 1,
                               AclPrivilege::kNone));
  return 0;
}
