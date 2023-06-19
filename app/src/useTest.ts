/* eslint-disable no-bitwise */

import { useEffect } from 'react';
import * as ExpoDevice from 'expo-device';
import { useMemo, useRef, useState } from 'react';
import { PermissionsAndroid, Platform } from 'react-native';
import base64 from 'react-native-base64';
import { BleManager, Device, Subscription } from 'react-native-ble-plx';

type State = {
  active: boolean;
  flashIndex: number;
  pattern: number[];
};

export const useTest = () => {
  const bleManager = useMemo(() => new BleManager(), []);
  const [connectedDevice, setConnectedDevice] = useState<Device | null>(null);

  const [allDevices, setAllDevices] = useState<Device[]>([]);
  const [state, setState] = useState<State | null>(null);
  const subscriptionRef = useRef<Subscription | null>(null);
  const lastDataRef = useRef<string>('');

  const requestAndroid31Permissions = async () => {
    const bluetoothScanPermission = await PermissionsAndroid.request(
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
      {
        title: 'Scan Permission',
        message: 'This app requires Bluetooth Scanning',
        buttonPositive: 'OK',
      }
    );
    const bluetoothConnectPermission = await PermissionsAndroid.request(
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
      {
        title: 'Connection Permission',
        message: 'This app requires Bluetooth Connecting',
        buttonPositive: 'OK',
      }
    );
    const bluetoothFinePermission = await PermissionsAndroid.request(
      PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
      {
        title: 'Fine Location',
        message: 'This app requires fine location',
        buttonPositive: 'OK',
      }
    );

    return (
      bluetoothScanPermission === PermissionsAndroid.RESULTS.GRANTED &&
      bluetoothConnectPermission === PermissionsAndroid.RESULTS.GRANTED &&
      bluetoothFinePermission === PermissionsAndroid.RESULTS.GRANTED
    );
  };

  const requestPermissions = async () => {
    if (Platform.OS === 'android') {
      if ((ExpoDevice.platformApiLevel ?? -1) < 31) {
        const bluetoothFinePermission = await PermissionsAndroid.request(
          PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
          {
            title: 'Location Permission',
            message: 'Bluetooth requires location',
            buttonPositive: 'OK',
          }
        );

        return bluetoothFinePermission === PermissionsAndroid.RESULTS.GRANTED;
      }

      return await requestAndroid31Permissions();
    }

    return true;
  };

  const isDuplicateDevice = (devices: Device[], nextDevice: Device) => {
    return devices.find((device) => device.id === nextDevice.id);
  };

  useEffect(() => {
    console.log('Start');
    (async function () {
      const hasPermissions = await requestPermissions();
      console.log('hasPermissions', hasPermissions);
      if (!hasPermissions) return;

      bleManager.startDeviceScan(null, null, async (error, device) => {
        if (error) {
          console.log(error);
          return;
        }

        // if (device) {
        //   if (await device.isConnected()) {
        //     setConnectedDevice(device);
        //   }

        //   setAllDevices((prevDevices) => {
        //     if (isDuplicateDevice(prevDevices, device)) {
        //       return prevDevices;
        //     }

        //     return [...prevDevices, device];
        //   });
        // }

        if (!device) return;
        if (device.name !== 'Ney Tack') return;

        console.log('device', device.name, device.id);

        bleManager.stopDeviceScan();

        // const device = await bleManager.connectToDevice(deviceId);
        await device.connect();

        console.log('connected');

        await device.discoverAllServicesAndCharacteristics();
        console.log('discovered');

        const [service] = await device.services();

        if (!service) return;

        console.log('service', service.uuid);

        const [characteristic] = await service.characteristics();

        if (!characteristic) return;
        console.log('characteristic', characteristic.uuid);

        // const subscription = characteristic.monitor((error, characteristic) => {
        //   if (error) {
        //     console.log('error', error);
        //     return;
        //   }
        //   console.log('characteristic', characteristic);

        //   if (!characteristic) return;

        //   console.log('characteristic', characteristic.value);

        //   const data = base64.decode(characteristic.value ?? '');
        //   console.log('data', data);
        // });
        const subscription = bleManager.monitorCharacteristicForDevice(
          device.id,
          service.uuid,
          characteristic.uuid,
          (error, characteristic) => {
            if (error) {
              console.log('error', error);
              return;
            }
            console.log('characteristic', characteristic);

            if (!characteristic) return;

            console.log('characteristic', characteristic.value);

            const data = base64.decode(characteristic.value ?? '');
            console.log('data', data);
          }
        );
      });
    })();
  }, []);
};
