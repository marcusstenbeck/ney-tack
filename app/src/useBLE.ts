/* eslint-disable no-bitwise */

import * as ExpoDevice from 'expo-device';
import { useMemo, useRef, useState } from 'react';
import { PermissionsAndroid, Platform } from 'react-native';
import base64 from 'react-native-base64';
import { BleManager, Device, Subscription } from 'react-native-ble-plx';

const logWithThrottle = (msg: any, delay: number) => {
  const now = Date.now();
  if (
    !logWithThrottle.lastLogTime ||
    now - logWithThrottle.lastLogTime >= delay
  ) {
    console.log(msg);
    logWithThrottle.lastLogTime = now;
  }
};

logWithThrottle.lastLogTime = 0;

const PICO_SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const PICO_CHARACTERISTIC_TX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
const PICO_CHARACTERISTIC_RX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

type State = {
  active: boolean;
  flashIndex: number;
  pattern: number[];
};

interface BluetoothLowEnergyApi {
  requestPermissions(): Promise<boolean>;
  scanForPeripherals(): void;
  stopScanningForPeripherals(): void;
  allDevices: Device[];
  connectedDevice: Device | null;
  connectToDevice(id: string): Promise<void>;
  disconnectDevice(): Promise<void>;
  startStreamingData(): void;
  send(data: any): Promise<void>;
  state: State | null;
}

export function useBLE(): BluetoothLowEnergyApi {
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

  const scanForPeripherals = async () => {
    bleManager.startDeviceScan(null, null, async (error, device) => {
      if (error) {
        console.log(error);
        return;
      }

      if (device) {
        if (await device.isConnected()) {
          setConnectedDevice(device);
        }

        setAllDevices((prevDevices) => {
          if (isDuplicateDevice(prevDevices, device)) {
            return prevDevices;
          }

          return [...prevDevices, device];
        });
      }
    });
  };

  const stopScanningForPeripherals = () => {
    bleManager.stopDeviceScan();
  };

  const connectToDevice = async (deviceId: string) => {
    try {
      const device = await bleManager.connectToDevice(deviceId);
      setConnectedDevice(device);
      await device.discoverAllServicesAndCharacteristics();
    } catch (error) {
      console.log(error);
    }
  };

  const disconnectDevice = async () => {
    if (!connectedDevice) return;
    try {
      console.log(await connectedDevice.cancelConnection());
      setConnectedDevice(null);
    } catch (error) {
      console.log(error);
    }
  };

  const startStreamingData = async () => {
    if (!connectedDevice) {
      console.log('No connected device');
      return;
    }

    if (subscriptionRef.current) {
      console.log('Already streaming data for this device');
      return;
    }

    subscriptionRef.current = connectedDevice.monitorCharacteristicForService(
      PICO_SERVICE,
      PICO_CHARACTERISTIC_RX,
      (error, characteristic) => {
        if (error) {
          console.log(error);
          return;
        }
        if (!characteristic?.value) {
          console.log('No data received');
          return;
        }

        if (lastDataRef.current === characteristic.value) {
          return;
        }

        lastDataRef.current = characteristic.value;

        const rawData = base64.decode(characteristic.value);

        const u8_arr = new Uint8Array(rawData.length);
        for (let i = 0; i < rawData.length; i++) {
          u8_arr[i] = rawData.charCodeAt(i);
        }

        // console.log(u8_arr);
        // logWithThrottle(u8_arr, 1000);
        const buffer = u8_arr.buffer;

        const dataView = new DataView(buffer);

        let offset = 0;
        const active = dataView.getUint8(offset);
        offset += 1;
        const flashIndex = dataView.getUint8(offset);
        offset += 1;
        const pattern_length = dataView.getUint8(offset);
        offset += 1;
        let pattern = new Array(pattern_length);
        for (let i = 0; i < pattern_length; i++) {
          pattern[i] = dataView.getUint16(offset);
          offset += 2;
        }

        setState({
          active: !!active,
          flashIndex,
          pattern,
        });
      }
    );
  };

  const send = async (data: Uint8Array) => {
    if (!connectedDevice) {
      console.log('No connected device');
      return;
    }

    try {
      await connectedDevice.writeCharacteristicWithoutResponseForService(
        PICO_SERVICE,
        PICO_CHARACTERISTIC_TX,
        base64.encodeFromByteArray(data)
      );
    } catch (error) {
      console.log(error);
    }
  };

  return {
    requestPermissions,
    scanForPeripherals,
    stopScanningForPeripherals,
    connectedDevice,
    connectToDevice,
    disconnectDevice,
    allDevices,
    startStreamingData,
    send,
    state,
  };
}
