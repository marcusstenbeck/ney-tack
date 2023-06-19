import {
  Alert,
  StatusBar,
  Button,
  SafeAreaView,
  ScrollView,
  StyleSheet,
  Text,
  Touchable,
  TouchableOpacity,
  View,
  Switch,
} from 'react-native';
import AsyncStorage from '@react-native-async-storage/async-storage';
import { useBLE } from './useBLE';
import { useEffect } from 'react';

export default function App() {
  const {
    requestPermissions,
    scanForPeripherals,
    stopScanningForPeripherals,
    allDevices,
    connectToDevice,
    connectedDevice,
    disconnectDevice,
    startStreamingData,
    send,
    state,
  } = useBLE();

  // connect if device id is stored in async storage
  useEffect(() => {
    const connectToStoredDevice = async () => {
      const storedDeviceId = await AsyncStorage.getItem('device_id');
      if (storedDeviceId) {
        try {
          await connectToDevice(storedDeviceId);
        } catch (error: any) {
          Alert.alert('Error', error.message);
        }
      }
    };
    connectToStoredDevice();
  }, []);

  const scanForDevices = async () => {
    const isPermissionsGranted = await requestPermissions();
    if (!isPermissionsGranted) return;
    scanForPeripherals();
  };

  return (
    <SafeAreaView style={styles.container}>
      <ScrollView style={styles.scrollView}>
        <StatusBar barStyle="default" />
        {!connectedDevice && (
          <>
            <Button title="Scan" onPress={scanForDevices} />
            {allDevices.length === 0 ? (
              <Text>No devices found</Text>
            ) : (
              allDevices.map((device) => (
                <TouchableOpacity
                  style={{
                    marginTop: 8,
                    marginBottom: 8,
                    marginLeft: 16,
                    marginRight: 16,
                    alignSelf: 'stretch',
                  }}
                  key={device.id}
                  onPress={async () => {
                    try {
                      await connectToDevice(device.id);
                      // store id in async storage
                      await AsyncStorage.setItem('device_id', device.id);
                      stopScanningForPeripherals();
                    } catch (error: any) {
                      Alert.alert('Error', error.message);
                    }
                  }}
                >
                  <Text style={{ color: 'blue', fontSize: 16 }}>
                    {device.name ?? 'No name'}
                  </Text>
                  <Text style={{ fontSize: 10, color: '#666' }}>
                    {device.id}
                  </Text>
                </TouchableOpacity>
              ))
            )}
          </>
        )}

        {connectedDevice && (
          <>
            <View
              style={{
                flexDirection: 'column',
              }}
            >
              <Text>{connectedDevice.name}</Text>
              <Text style={{ fontSize: 10, color: '#666' }}>
                {connectedDevice.id}
              </Text>
            </View>
            <Button title="Start streaming" onPress={startStreamingData} />

            <Button
              title="Disconnect"
              onPress={async () => {
                try {
                  await disconnectDevice();
                  // remove id from async storage
                  await AsyncStorage.removeItem('device_id');
                  Alert.alert('Disconnected');
                } catch (error: any) {
                  Alert.alert('Error', error.message);
                }
              }}
            />

            <View
              style={{
                flexDirection: 'row',
                justifyContent: 'flex-end',
                alignItems: 'flex-end',
                gap: 16,
                margin: 16,
              }}
            >
              <Text
                style={{
                  fontSize: 24,
                  fontWeight: 'bold',
                }}
              >
                {state?.active ? 'On' : 'Off'}
              </Text>

              <Switch
                value={!!state?.active}
                onValueChange={async () => {
                  await send(new Uint8Array([42]));
                }}
              />
            </View>

            <Text>{JSON.stringify(state)}</Text>
          </>
        )}
      </ScrollView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    paddingTop: StatusBar.currentHeight,
  },
  scrollView: {},
});
