std_msgs/Header header

uint8 STATUS_OK=0
uint8 STATUS_WARN=1
uint8 STATUS_FAULT=2
uint8 STATUS_E_STOP=3

uint16 seq_echo
uint8 status
uint16 fault_code

float32 bus_voltage
float32 loop_time_ms
float32 spi_latency_ms
uint32 packet_drop_count
uint32 crc_error_count
uint32 missed_deadline_count

bool torque_enabled
bool spi_connected
bool servo_connected
