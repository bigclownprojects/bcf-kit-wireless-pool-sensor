#include <application.h>
#include <twr_ds18b20.h>

/*

 SENSOR MODULE CONNECTION
==========================

Sensor Module R1.0 - 4 pin connector
VCC, GND, - , DATA

Sensor Module R1.1 - 5 pin connector
- , GND , VCC , - , DATA


 DS18B20 sensor pinout
=======================
VCC - red
GND - black
DATA- yellow (white)

*/

// Time after the sending is less frequent to save battery
#define SERVICE_INTERVAL_INTERVAL (10 * 60 * 1000)
#define BATTERY_UPDATE_INTERVAL   (30 * 60 * 1000)

#define UPDATE_SERVICE_INTERVAL            (5 * 1000)
#define UPDATE_NORMAL_INTERVAL             (1 * 60 * 1000)

#define BAROMETER_UPDATE_SERVICE_INTERVAL  (1 * 60 * 1000)
#define BAROMETER_UPDATE_NORMAL_INTERVAL   (5 * 60 * 1000)

#define TEMPERATURE_DS18B20_PUB_NO_CHANGE_INTERVAL (5 * 60 * 1000)
#define TEMPERATURE_DS18B20_PUB_VALUE_CHANGE 1.0f //0.4f

#define TEMPERATURE_TAG_PUB_NO_CHANGE_INTERVAL (5 * 60 * 1000)
#define TEMPERATURE_TAG_PUB_VALUE_CHANGE 50.0f //0.6f

#define HUMIDITY_TAG_PUB_NO_CHANGE_INTERVAL (5 * 60 * 1000)
#define HUMIDITY_TAG_PUB_VALUE_CHANGE 50.0f //50.f

#define LUX_METER_TAG_PUB_NO_CHANGE_INTERVAL (5 * 60 * 1000)
#define LUX_METER_TAG_PUB_VALUE_CHANGE 100000.0f // set too big value so data will be send only every 5 minutes, light changes a lot outside.

#define BAROMETER_TAG_PUB_NO_CHANGE_INTERVAL (5 * 60 * 1000)
#define BAROMETER_TAG_PUB_VALUE_CHANGE 200000.0f //20.0f

#define DS18B20_SENSOR_COUNT 14

static twr_led_t led;
static twr_button_t button;

static twr_ds18b20_t ds18b20;
static twr_ds18b20_sensor_t ds18b20_sensors[DS18B20_SENSOR_COUNT];

struct {
    event_param_t temperature;
    event_param_t temperature_ds18b20[DS18B20_SENSOR_COUNT];
    event_param_t humidity;
    event_param_t illuminance;
    event_param_t pressure;

} params;

void handler_button(twr_button_t *s, twr_button_event_t e, void *p);

void handler_battery(twr_module_battery_event_t e, void *p);

void handler_ds18b20(twr_ds18b20_t *s, uint64_t device_id, twr_ds18b20_event_t e, void *p);

void climate_module_event_handler(twr_module_climate_event_t event, void *event_param);

void switch_to_normal_mode_task(void *param);

void handler_button(twr_button_t *s, twr_button_event_t e, void *p)
{
    (void) s;
    (void) p;

    if (e == TWR_BUTTON_EVENT_PRESS)
    {
        twr_led_pulse(&led, 100);

        static uint16_t event_count = 0;

        twr_radio_pub_push_button(&event_count);

        event_count++;
    }
}

void handler_battery(twr_module_battery_event_t e, void *p)
{
    (void) e;
    (void) p;

    float voltage;

    if (twr_module_battery_get_voltage(&voltage))
    {
        twr_radio_pub_battery(&voltage);
    }
}

void handler_ds18b20(twr_ds18b20_t *self, uint64_t device_address, twr_ds18b20_event_t e, void *p)
{
    (void) p;

    float value = NAN;

    if (e == TWR_DS18B20_EVENT_UPDATE)
    {
        twr_ds18b20_get_temperature_celsius(self, device_address, &value);
        int device_index = twr_ds18b20_get_index_by_device_address(self, device_address);

        //twr_log_debug("UPDATE %" PRIx64 "(%d) = %f", device_address, device_index, value);

        if ((fabs(value - params.temperature_ds18b20[device_index].value) >= TEMPERATURE_DS18B20_PUB_VALUE_CHANGE) || (params.temperature_ds18b20[device_index].next_pub < twr_scheduler_get_spin_tick()))
        {
            static char topic[64];
            snprintf(topic, sizeof(topic), "thermometer/%016" PRIx64 "/temperature", device_address);
            twr_radio_pub_float(topic, &value);
            params.temperature_ds18b20[device_index].value = value;
            params.temperature_ds18b20[device_index].next_pub = twr_scheduler_get_spin_tick() + TEMPERATURE_DS18B20_PUB_NO_CHANGE_INTERVAL;
        }
    }

    if (e == TWR_DS18B20_EVENT_ERROR)
    {
        //twr_log_debug("twr_ds18b20_EVENT_ERROR");
    }
}

void climate_module_event_handler(twr_module_climate_event_t event, void *event_param)
{
    (void) event_param;

    float value;

    if (event == TWR_MODULE_CLIMATE_EVENT_UPDATE_THERMOMETER)
    {
        if (twr_module_climate_get_temperature_celsius(&value))
        {
            if ((fabs(value - params.temperature.value) >= TEMPERATURE_TAG_PUB_VALUE_CHANGE) || (params.temperature.next_pub < twr_scheduler_get_spin_tick()))
            {
                twr_radio_pub_temperature(TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT, &value);
                params.temperature.value = value;
                params.temperature.next_pub = twr_scheduler_get_spin_tick() + TEMPERATURE_TAG_PUB_NO_CHANGE_INTERVAL;
            }
        }
    }
    else if (event == TWR_MODULE_CLIMATE_EVENT_UPDATE_HYGROMETER)
    {
        if (twr_module_climate_get_humidity_percentage(&value))
        {
            if ((fabs(value - params.humidity.value) >= HUMIDITY_TAG_PUB_VALUE_CHANGE) || (params.humidity.next_pub < twr_scheduler_get_spin_tick()))
            {
                twr_radio_pub_humidity(TWR_RADIO_PUB_CHANNEL_R3_I2C0_ADDRESS_DEFAULT, &value);
                params.humidity.value = value;
                params.humidity.next_pub = twr_scheduler_get_spin_tick() + HUMIDITY_TAG_PUB_NO_CHANGE_INTERVAL;
            }
        }
    }
    else if (event == TWR_MODULE_CLIMATE_EVENT_UPDATE_LUX_METER)
    {
        if (twr_module_climate_get_illuminance_lux(&value))
        {
            if (value < 1)
            {
                value = 0;
            }
            if ((fabs(value - params.illuminance.value) >= LUX_METER_TAG_PUB_VALUE_CHANGE) || (params.illuminance.next_pub < twr_scheduler_get_spin_tick()))
            {
                twr_radio_pub_luminosity(TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT, &value);
                params.illuminance.value = value;
                params.illuminance.next_pub = twr_scheduler_get_spin_tick() + LUX_METER_TAG_PUB_NO_CHANGE_INTERVAL;
            }
        }
    }
    else if (event == TWR_MODULE_CLIMATE_EVENT_UPDATE_BAROMETER)
    {
        if (twr_module_climate_get_pressure_pascal(&value))
        {
            if ((fabs(value - params.pressure.value) >= BAROMETER_TAG_PUB_VALUE_CHANGE) || (params.pressure.next_pub < twr_scheduler_get_spin_tick()))
            {
                float meter;

                if (!twr_module_climate_get_altitude_meter(&meter))
                {
                    return;
                }

                twr_radio_pub_barometer(TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT, &value, &meter);
                params.pressure.value = value;
                params.pressure.next_pub = twr_scheduler_get_spin_tick() + BAROMETER_TAG_PUB_NO_CHANGE_INTERVAL;
            }
        }
    }
}

// This task is fired once after the SERVICE_INTERVAL_INTERVAL milliseconds and changes the period
// of measurement. After module power-up you get faster updates so you can test the module and see
// instant changes. After SERVICE_INTERVAL_INTERVAL the update period is longer to save batteries.
void switch_to_normal_mode_task(void *param)
{
    twr_module_climate_set_update_interval_thermometer(UPDATE_NORMAL_INTERVAL);
    twr_module_climate_set_update_interval_hygrometer(UPDATE_NORMAL_INTERVAL);
    twr_module_climate_set_update_interval_lux_meter(UPDATE_NORMAL_INTERVAL);
    twr_module_climate_set_update_interval_barometer(BAROMETER_UPDATE_SERVICE_INTERVAL);

    twr_ds18b20_set_update_interval(&ds18b20, UPDATE_NORMAL_INTERVAL);

    twr_scheduler_unregister(twr_scheduler_get_current_task_id());
}

void application_init(void)
{

    //twr_log_init(TWR_LOG_LEVEL_DUMP, TWR_LOG_TIMESTAMP_ABS);

    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_set_mode(&led, TWR_LED_MODE_OFF);

    twr_radio_init(TWR_RADIO_MODE_NODE_SLEEPING);

    twr_button_init(&button, TWR_GPIO_BUTTON, TWR_GPIO_PULL_DOWN, false);
    twr_button_set_event_handler(&button, handler_button, NULL);

    twr_module_battery_init();
    twr_module_battery_set_event_handler(handler_battery, NULL);
    twr_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // For single sensor you can call twr_ds18b20_init()
    //twr_ds18b20_init(&ds18b20, TWR_DS18B20_RESOLUTION_BITS_12);
    twr_ds18b20_init_multiple(&ds18b20, ds18b20_sensors, DS18B20_SENSOR_COUNT, TWR_DS18B20_RESOLUTION_BITS_12);

    twr_ds18b20_set_event_handler(&ds18b20, handler_ds18b20, NULL);
    twr_ds18b20_set_update_interval(&ds18b20, UPDATE_SERVICE_INTERVAL);

    // Initialize climate module
    twr_module_climate_init();
    twr_module_climate_set_event_handler(climate_module_event_handler, NULL);
    twr_module_climate_set_update_interval_thermometer(UPDATE_SERVICE_INTERVAL);
    twr_module_climate_set_update_interval_hygrometer(UPDATE_SERVICE_INTERVAL);
    twr_module_climate_set_update_interval_lux_meter(UPDATE_SERVICE_INTERVAL);
    twr_module_climate_set_update_interval_barometer(BAROMETER_UPDATE_NORMAL_INTERVAL);
    twr_module_climate_measure_all_sensors();

    twr_scheduler_register(switch_to_normal_mode_task, NULL, SERVICE_INTERVAL_INTERVAL);

    twr_radio_pairing_request("radio-pool-sensor", FW_VERSION);

    twr_led_pulse(&led, 2000);

}
