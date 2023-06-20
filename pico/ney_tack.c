#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include "btstack.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "mygatt.h"
#include "ble/gatt-service/nordic_spp_service_server.h"
#include "ltr303_i2c.h"

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE -1
#endif

#define REPORT_INTERVAL_MS 3000
#define STATE_CHECK_INTERVAL_MS 300

#define FLASHER_STATE_OFF 0
#define FLASHER_STATE_ON 1

// *****************************************************************************
// Type Definitions
// *****************************************************************************

typedef struct
{
  char name;
  int le_notification_enabled;
  hci_con_handle_t connection_handle;
  int counter;
  char test_data[200];
  int test_data_len;
  uint32_t test_data_sent;
  uint32_t test_data_start;
  btstack_context_callback_registration_t send_request;
} nordic_spp_le_streamer_connection_t;

typedef struct
{
  uint8_t active;
  uint8_t flash_index;
  uint8_t pattern_length;
  uint16_t pattern[16];
} State;

// *****************************************************************************
// Global Variables
// *****************************************************************************

State STATE = {
    .active = 0,
    .pattern_length = 4,
    .pattern = {1000, 1000, 250, 250},
};

uint8_t serialized_state[sizeof(STATE)];
int serialized_state_len = 0;

int led_state = 0;
int flasher_state = 0;
const uint LED_PIN = 21;

static btstack_timer_source_t state_check_timer;
static btstack_timer_source_t flasher_timer;

const uint8_t adv_data[] = {
    // Flags indicating the device's capabilities (general discoverable mode and BR/EDR not supported)
    2,
    BLUETOOTH_DATA_TYPE_FLAGS,
    0x06,
    // Complete local name of the device ("nRF SPP")
    9,
    BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'N',
    'e',
    'y',
    ' ',
    'T',
    'a',
    'c',
    'k',
    // Complete list of 128-bit service class UUIDs (Nordic UART Service)
    17,
    BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
    0x9e,
    0xca,
    0xdc,
    0x24,
    0xe,
    0xe5,
    0xa9,
    0xe0,
    0x93,
    0xf3,
    0xa3,
    0xb5,
    0x1,
    0x0,
    0x40,
    0x6e,
};
const uint8_t adv_data_len = sizeof(adv_data);

static btstack_packet_callback_registration_t hci_event_callback_registration;
static nordic_spp_le_streamer_connection_t nordic_spp_le_streamer_connection;

// *****************************************************************************
// Function Declarations
// *****************************************************************************

void led_toggle();
void led_set(int state);

static void nordic_spp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void att_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void nordic_can_send(void *some_context);
static void init_connection();
static nordic_spp_le_streamer_connection_t *connection_for_conn_handle(hci_con_handle_t con_handle);
static void test_reset(nordic_spp_le_streamer_connection_t *context);
static void test_track_sent(nordic_spp_le_streamer_connection_t *context, int bytes_sent);
void serialize_state(State state, uint8_t *serialized_data, int *serialized_data_len);

static void state_check_handler(btstack_timer_source_t *ts);
static void flasher_handler(btstack_timer_source_t *ts);
static void start_flasher();
static int flash_tick();

// *****************************************************************************
// Main
// *****************************************************************************

int main()
{
  stdio_init_all();

  if (cyw43_arch_init())
  {
    printf("Wi-Fi init failed");
    return EXIT_FAILURE;
  }

  gpio_init(22);
  gpio_pull_down(22);
  gpio_set_dir(22, GPIO_IN);

  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);

  if (ltr303_i2c_init())
  {
    printf("Failed to initialize LTR303\n");
    return EXIT_FAILURE;
  }

  uint16_t visible_and_ir;
  uint16_t ir_only;
  uint8_t motion_pin;

  while (true)
  {
    if (!ltr303_i2c_has_new_data())
    {
      continue;
    }

    if (ltr303_i2c_read_both_channels(&visible_and_ir, &ir_only))
    {
      continue;
    }

    motion_pin = gpio_get(22);

    led_set(motion_pin);

    printf("motion_pin: %d\n", motion_pin);
    printf("visible_and_ir: %d\n", visible_and_ir);
    printf("ir_only: %d\n", ir_only);
    printf("\n");

    sleep_ms(250);
  }

  // will be called when a Bluetooth event is received by the Bluetooth controller
  hci_event_callback_registration.callback = &hci_packet_handler;
  hci_add_event_handler(&hci_event_callback_registration);

  // set one-shot timer
  state_check_timer.process = &state_check_handler;
  btstack_run_loop_set_timer(&state_check_timer, STATE_CHECK_INTERVAL_MS);
  btstack_run_loop_add_timer(&state_check_timer);

  flasher_timer.process = &flasher_handler;

  // By calling l2cap_init(), the Bluetooth stack is initialized and ready to establish L2CAP
  // connections with other Bluetooth devices.
  l2cap_init();

  // sm_init() is a function call that initializes the Security Manager (SM) of the Bluetooth stack.
  //
  // The SM is responsible for managing the security aspects of Bluetooth connections, such as
  // pairing, encryption, and authentication. By calling sm_init(), the SM is initialized and ready
  // to handle security-related events and requests from other Bluetooth devices.
  //
  // This function should be called once during the initialization of the Bluetooth stack, before
  // any other security-related functions are called.
  sm_init();

  // att_server_init() is a function call that initializes the Attribute Protocol (ATT) server of
  // the Bluetooth stack.
  // The ATT server is responsible for managing the attributes of a Bluetooth device, such as its
  // services, characteristics, and descriptors. By calling att_server_init(), the ATT server is
  // initialized and ready to handle attribute-related events and requests from other Bluetooth
  // devices.
  // The `profile_data` argument is a pointer to the ATT DB of the Bluetooth device. It's located in
  // mygatt.h.
  att_server_init(profile_data, NULL, NULL);

  // nordic_spp_service_server_init() is a function call that initializes the Nordic SPP (Serial
  // Port Profile) service server of the Bluetooth stack.
  // The Nordic SPP service is a custom Bluetooth service that allows a Bluetooth device to emulate
  // a serial port over a Bluetooth connection. This can be useful for applications that require a
  // wireless serial connection, such as wireless debugging or data transfer.
  nordic_spp_service_server_init(&nordic_spp_packet_handler);

  // att_server_register_packet_handler() is a function call that registers a packet handler
  // function with the Attribute Protocol (ATT) server of the Bluetooth stack.
  // The packet handler function is responsible for processing incoming ATT packets and generating
  // outgoing ATT packets. When an ATT packet is received by the Bluetooth stack, the packet handler
  // function is called with the packet data as its argument. The packet handler function should
  // then process the packet and generate a response, if necessary.
  att_server_register_packet_handler(att_packet_handler);

  // adv_int_min and adv_int_max used to set the advertising interval of a Bluetooth Low Energy
  // (BLE) device. The advertising interval determines how often the device broadcasts its presence
  // to other BLE devices in the vicinity.
  // The advertising interval is specified in units of 0.625 milliseconds, so 48 * 0.625 = 30 ms.
  // By setting adv_int_min and adv_int_max to the same value, the device will use a fixed
  // advertising interval.
  uint16_t adv_int_min = 800;
  uint16_t adv_int_max = 800;

  // There are several types of advertising that can be used in Bluetooth Low Energy (BLE), such as
  // connectable advertising, non-connectable advertising, and scannable advertising.
  // adv_type values are:
  //   - 0x00: Connectable advertising, which means that other BLE devices can connect to the device
  //           and establish a connection.
  //   - 0x01: Scannable advertising, which means that other BLE devices can scan for the device but
  //           cannot connect to it.
  //   - 0x02: Non-connectable advertising, which means that other BLE devices can only receive the
  //           advertising data but cannot establish a connection.
  //   - 0x03: Scan response, which means that the device is responding to a scan request from
  //           another BLE device.
  uint8_t adv_type = 0; // use connectable advertising: other devices can connect to this device

  // prepare an empty address buffer
  bd_addr_t null_addr;
  memset(null_addr, 0, 6);

  gap_advertisements_set_params(
      adv_int_min, // Set the minimum advertising interval
      adv_int_max, // Set the maximum advertising interval
      adv_type,    // Set the type of advertising (connectable, non-connectable, etc.)
      0,           // Set the type of direct address (not used in this case)
      null_addr,   // Set the direct address (not used in this case)
      0x07,        // Set the advertising channel map to use all three BLE advertising channels
      0x00         // Set the advertising filter policy to not use a whitelist
  );
  // The advertising data array contains information about the device that is broadcast to other
  // Bluetooth devices during advertising.
  gap_advertisements_set_data(adv_data_len, (uint8_t *)adv_data);
  // Enable broadcast advertising data to other Bluetooth devices
  gap_advertisements_enable(1);

  init_connection();

  // By calling hci_power_control() with the HCI_POWER_ON argument, the Bluetooth controller is
  // turned on and is ready to be used for communication with other Bluetooth devices.
  hci_power_control(HCI_POWER_ON);

  // By calling btstack_run_loop_execute(), the BTstack run loop is started, and the Bluetooth
  // device is ready to handle incoming Bluetooth events and callbacks. This function call is
  // typically used at the end of the initialization process to start the Bluetooth stack and begin
  // handling Bluetooth events.
  btstack_run_loop_execute();

  return EXIT_SUCCESS;
}

// *****************************************************************************
// Function Definitions
// *****************************************************************************

void led_toggle()
{
  led_set(!led_state);
}

void led_set(int state)
{
  if (led_state == state)
  {
    return;
  }
  led_state = state;
  gpio_put(LED_PIN, led_state);
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
}

// This function is called when a Nordic SPP packet is received
static void nordic_spp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
  hci_con_handle_t con_handle;
  nordic_spp_le_streamer_connection_t *context;

  // Handle different types of packets
  switch (packet_type)
  {
  case HCI_EVENT_PACKET:
    // Handle HCI event packets
    if (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META)
    {
      break;
    }

    switch (hci_event_gattservice_meta_get_subevent_code(packet))
    {

    case GATTSERVICE_SUBEVENT_SPP_SERVICE_CONNECTED:
      // Handle SPP service connected event
      // Get the connection handle from the event packet
      con_handle = gattservice_subevent_spp_service_connected_get_con_handle(packet);
      // Get the connection context for the connection handle
      context = connection_for_conn_handle(con_handle);
      if (!context)
      {
        break;
      }
      // Enable LE notification
      context->le_notification_enabled = 1;
      // Reset the test
      test_reset(context);
      // Set the send request callback to nordic_can_send
      context->send_request.callback = &nordic_can_send;
      // Request to send data
      nordic_spp_service_server_request_can_send_now(&context->send_request, context->connection_handle);
      break;

    case GATTSERVICE_SUBEVENT_SPP_SERVICE_DISCONNECTED:
      // Handle SPP service disconnected event
      // Set the connection handle to HCI_CON_HANDLE_INVALID
      con_handle = HCI_CON_HANDLE_INVALID;
      // Get the connection context for the connection handle
      context = connection_for_conn_handle(con_handle);
      if (!context)
      {
        break;
      }
      // Disable LE notification
      context->le_notification_enabled = 0;
      break;

    default:
      // Do nothing for other types of GATTSERVICE meta events
      break;
    }
    break;

  case RFCOMM_DATA_PACKET:
    // Handle RFCOMM data packets
    printf("RECV: ");
    printf_hexdump(packet, size);

    // TODO: Add code to handle received data
    if (STATE.active == 0)
    {
      STATE.active = 1;
    }
    else
    {
      STATE.active = 0;
    }

    // Get the connection context for the channel
    context = connection_for_conn_handle((hci_con_handle_t)channel);
    if (!context)
    {
      break;
    }
    // Track the sent data
    test_track_sent(context, size);
    break;

  default:
    // Do nothing for other types of packets
    break;
  }
}

// This function is called when an ATT event is received by the Bluetooth controller
static void att_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
  // Suppress warnings about unused variables
  UNUSED(channel);
  UNUSED(size);

  // Check if the packet is an HCI event packet
  if (packet_type != HCI_EVENT_PACKET)
    return;

  int mtu;
  nordic_spp_le_streamer_connection_t *context;

  // Handle different types of ATT events
  switch (hci_event_packet_get_type(packet))
  {

  case ATT_EVENT_CONNECTED:
    // Set up a new connection
    context = connection_for_conn_handle(HCI_CON_HANDLE_INVALID);
    if (!context)
      break;
    // Initialize the connection properties
    context->counter = 'A';
    context->test_data_len = ATT_DEFAULT_MTU - 4; // -1 for nordic 0x01 packet type
    context->connection_handle = att_event_connected_get_handle(packet);
    break;

  case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
    // Handle MTU exchange complete event
    mtu = att_event_mtu_exchange_complete_get_MTU(packet) - 3;
    context = connection_for_conn_handle(att_event_mtu_exchange_complete_get_handle(packet));
    if (!context)
      break;
    // Set the test data length based on the MTU
    context->test_data_len = btstack_min(mtu - 3, sizeof(context->test_data));
    // Print a debug message
    printf("%c: ATT MTU = %u => use test data of len %u\n", context->name, mtu, context->test_data_len);
    break;

  case ATT_EVENT_DISCONNECTED:
    // Handle disconnection event
    context = connection_for_conn_handle(att_event_disconnected_get_handle(packet));
    if (!context)
      break;
    // Free the connection by setting the connection handle to HCI_CON_HANDLE_INVALID
    printf("%c: Disconnect\n", context->name);
    context->le_notification_enabled = 0;
    context->connection_handle = HCI_CON_HANDLE_INVALID;
    break;

  default:
    // Do nothing for other types of events
    break;
  }
}

static void init_connection()
{
  nordic_spp_le_streamer_connection.connection_handle = HCI_CON_HANDLE_INVALID;
  nordic_spp_le_streamer_connection.name = 'A';
}

static nordic_spp_le_streamer_connection_t *connection_for_conn_handle(hci_con_handle_t con_handle)
{
  if (nordic_spp_le_streamer_connection.connection_handle == con_handle)
  {
    return &nordic_spp_le_streamer_connection;
  }
  return NULL;
}

// This function is called when an HCI event is received by the Bluetooth controller
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
  // Suppress warnings about unused variables
  UNUSED(channel);
  UNUSED(size);

  uint16_t conn_interval;
  hci_con_handle_t con_handle;

  // Check if the packet is an HCI event packet
  if (packet_type != HCI_EVENT_PACKET)
    return;

  // Handle different types of HCI events
  switch (hci_event_packet_get_type(packet))
  {
  case BTSTACK_EVENT_STATE:
    // BTstack activated, get started
    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING)
    {
      // Print a message to the console to prompt the user to connect
      printf("To start the streaming, please run nRF Toolbox -> UART to connect.\n");
    }
    break;
  case HCI_EVENT_LE_META:
    // Handle different types of LE meta events
    switch (hci_event_le_meta_get_subevent_code(packet))
    {
    case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
      // Handle LE connection complete event
      // Get the connection handle and connection interval from the event packet
      con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
      conn_interval = hci_subevent_le_connection_complete_get_conn_interval(packet);
      // Print the connection interval and latency to the console
      printf("LE Connection - Connection Interval: %u.%02u ms\n", conn_interval * 125 / 100, 25 * (conn_interval & 3));
      printf("LE Connection - Connection Latency: %u\n", hci_subevent_le_connection_complete_get_conn_latency(packet));

      // Request a connection interval of 500 ms
      printf("LE Connection - Request 500 ms connection interval\n");
      gap_request_connection_parameter_update(con_handle, 400, 400, 0, 0x0048);
      break;
    case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
      // Handle LE connection update complete event
      // Get the connection handle and connection interval from the event packet
      con_handle = hci_subevent_le_connection_update_complete_get_connection_handle(packet);
      conn_interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
      // Print the updated connection interval and latency to the console
      printf("LE Connection - Connection Param update - connection interval %u.%02u ms, latency %u\n", conn_interval * 125 / 100,
             25 * (conn_interval & 3), hci_subevent_le_connection_update_complete_get_conn_latency(packet));
      break;
    default:
      // Do nothing for other types of LE meta events
      break;
    }
    break;
  default:
    // Do nothing for other types of HCI events
    break;
  }
}

// This function is called when the Bluetooth controller is ready to send data
static void nordic_can_send(void *some_context)
{
  // Suppress warnings about unused variables
  UNUSED(some_context);

  // Check if the connection is active and LE notification is enabled
  if ((nordic_spp_le_streamer_connection.connection_handle != HCI_CON_HANDLE_INVALID) &&
      (nordic_spp_le_streamer_connection.le_notification_enabled))
  {
    // do nothing
  }
  else
  {
    // If no active connection is found, return
    return;
  }

  nordic_spp_le_streamer_connection_t *context = &nordic_spp_le_streamer_connection;

  // Get the connection context for the current connection

  // Create test data
  // context->counter++;
  // if (context->counter > 'Z')
  //   context->counter = 'A';
  // memset(context->test_data, context->counter, context->test_data_len);

  serialize_state(STATE, serialized_state, &serialized_state_len);

  // Send the serialized data
  memcpy(context->test_data, serialized_state, serialized_state_len);
  context->test_data_len = serialized_state_len;

  // Send the test data
  nordic_spp_service_server_send(context->connection_handle, (uint8_t *)context->test_data, context->test_data_len);

  // Track the sent data
  test_track_sent(context, context->test_data_len);

  // Request the next send event
  nordic_spp_service_server_request_can_send_now(&context->send_request, context->connection_handle);
}

// This function resets the test data for a given connection
static void test_reset(nordic_spp_le_streamer_connection_t *context)
{
  // Set the start time for the test data to the current time
  context->test_data_start = btstack_run_loop_get_time_ms();
  // Reset the amount of test data sent to 0
  context->test_data_sent = 0;
}

// This function tracks the amount of test data sent for a given connection
static void test_track_sent(nordic_spp_le_streamer_connection_t *context, int bytes_sent)
{
  // Add the number of bytes sent to the total amount of test data sent
  context->test_data_sent += bytes_sent;

  // Get the current time
  uint32_t now = btstack_run_loop_get_time_ms();
  // Calculate the time passed since the test data was started
  uint32_t time_passed = now - context->test_data_start;

  // If the time passed is less than the report interval, return
  if (time_passed < REPORT_INTERVAL_MS)
    return;

  // Calculate the speed of the test data transfer in bytes per second
  int bytes_per_second = context->test_data_sent * 1000 / time_passed;

  // Print the speed of the test data transfer
  printf("%c: %" PRIu32 " bytes sent-> %u.%03u kB/s\n", context->name, context->test_data_sent, bytes_per_second / 1000, bytes_per_second % 1000);

  // Reset the start time and amount of test data sent
  context->test_data_start = now;
  context->test_data_sent = 0;
}

void serialize_state(State state, uint8_t *serialized_state, int *serialized_state_len)
{
  // Serialize the struct to a byte array
  uint8_t active = state.active;
  uint8_t flash_index = state.flash_index;
  uint16_t pattern[16];
  for (int i = 0; i < 16; i++)
  {
    pattern[i] = __builtin_bswap16(state.pattern[i]);
  }
  uint8_t pattern_length = state.pattern_length;
  int offset = 0;
  memcpy(serialized_state + offset, &active, sizeof(active));
  offset += sizeof(active);
  memcpy(serialized_state + offset, &flash_index, sizeof(active));
  offset += sizeof(active);
  memcpy(serialized_state + offset, &pattern_length, sizeof(pattern_length));
  offset += sizeof(pattern_length);
  memcpy(serialized_state + offset, pattern, sizeof(pattern));
  offset += sizeof(pattern);

  *serialized_state_len = offset;
}

static void state_check_handler(btstack_timer_source_t *ts)
{
  UNUSED(ts);

  if (STATE.active == 1)
  {
    start_flasher();
  }

  // re-register timer
  btstack_run_loop_set_timer(&state_check_timer, STATE_CHECK_INTERVAL_MS);
  btstack_run_loop_add_timer(&state_check_timer);
}

static void flasher_handler(btstack_timer_source_t *ts)
{
  UNUSED(ts);

  if (flasher_state == FLASHER_STATE_OFF)
  {
    // don't advance flash index if flash was on
    flasher_state = FLASHER_STATE_ON;
  }
  else
  {
    // advance flash index if flash was on
    STATE.flash_index = (STATE.flash_index + 1) % STATE.pattern_length;
  }

  int duration = flash_tick();

  if (duration < 0 || STATE.active == 0)
  {
    flasher_state = FLASHER_STATE_OFF;
    led_set(0);
    STATE.flash_index = 0;

    return;
  }

  // re-register timer
  btstack_run_loop_set_timer(&flasher_timer, duration);
  btstack_run_loop_add_timer(&flasher_timer);
}

static void start_flasher()
{
  if (flasher_state > 0)
  {
    return;
  }

  btstack_run_loop_set_timer(&flasher_timer, 0);
  btstack_run_loop_add_timer(&flasher_timer);
}

static int flash_tick()
{
  if (STATE.pattern_length == 0)
  {
    return -1;
  }

  if (STATE.flash_index % 2 == 0)
  {
    led_set(1);
  }
  else
  {
    led_set(0);
  }

  int duration = STATE.pattern[STATE.flash_index];

  return duration;
}