// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef DEVICE_BLUETOOTH_TEST_FAKE_REMOTE_GATT_SERVICE_H_
#define DEVICE_BLUETOOTH_TEST_FAKE_REMOTE_GATT_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "device/bluetooth/bluetooth_remote_gatt_service.h"
#include "device/bluetooth/bluetooth_uuid.h"
#include "device/bluetooth/public/interfaces/test/fake_bluetooth.mojom.h"
#include "device/bluetooth/test/fake_remote_gatt_characteristic.h"

namespace device {
class BluetoothDevice;
class BluetoothRemoteGattService;
class BluetoothRemoteGattCharacteristic;
}  // namespace device

namespace bluetooth {

// Implements device::BluetoothRemoteGattService. Meant to be used by
// FakePeripheral to keep track of the service's state and attributes.
class FakeRemoteGattService : public device::BluetoothRemoteGattService {
 public:
  FakeRemoteGattService(const std::string& service_id,
                        const device::BluetoothUUID& service_uuid,
                        bool is_primary,
                        device::BluetoothDevice* device);
  ~FakeRemoteGattService() override;

  // Adds a fake characteristic with |characteristic_uuid| and |properties|
  // to this service. Returns the characteristic's Id.
  std::string AddFakeCharacteristic(
      const device::BluetoothUUID& characteristic_uuid,
      mojom::CharacteristicPropertiesPtr properties);

  // device::BluetoothGattService overrides:
  std::string GetIdentifier() const override;
  device::BluetoothUUID GetUUID() const override;
  bool IsPrimary() const override;

  // device::BluetoothRemoteGattService overrides:
  device::BluetoothDevice* GetDevice() const override;
  std::vector<device::BluetoothRemoteGattCharacteristic*> GetCharacteristics()
      const override;
  std::vector<device::BluetoothRemoteGattService*> GetIncludedServices()
      const override;
  device::BluetoothRemoteGattCharacteristic* GetCharacteristic(
      const std::string& identifier) const override;

 private:
  const std::string service_id_;
  const device::BluetoothUUID service_uuid_;
  const bool is_primary_;
  device::BluetoothDevice* device_;

  size_t last_characteristic_id_;

  using FakeCharacteristicMap =
      std::map<std::string, std::unique_ptr<FakeRemoteGattCharacteristic>>;
  FakeCharacteristicMap fake_characteristics_;
};

}  // namespace bluetooth

#endif  // DEVICE_BLUETOOTH_TEST_FAKE_REMOTE_GATT_SERVICE_H_
