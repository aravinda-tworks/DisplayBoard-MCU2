/**************************************************************************/
/*!
@file     pressure_sensor.cpp

@brief 	  Pressure sensor module

@author   Tworks

@defgroup VentilatorModule

Module to read and measure pressure/flow rate 
from the pressure sensors
@{
*/
/**************************************************************************/

#include "pressure_sensor.h"
#include "ads1115_utils.h"

/*
* Macros to enable the sensor functionalities
*/

#define SENSOR_DISPLAY_REFRESH_TIME 500

/*
* Sensor specific configurations
*/
//#ifdef MPX5010
#define MPX5010_VS              (5.0)
#define MPX5010_VFSS            (0.0)
#define MPX5010_ACCURACY        (0.05 * MPX5010_VFSS)
#define MPX5010_ZERO_READING    2000 // 0.25 volts corresponds to 0.25/ADS115_MULTIPLIER = 2000 
#define MPX5010_ERROR_THRESHOLD 20
//#endif

//#ifdef MPXV7002DP
#define MPXV7002DP_VS           (5.0)
#define MPXV7002DP_VFSS         (4.0)
#define MPXV7002DP_ACCURACY     (0.06250)
#define MPX7002DP_ZERO_READING  22000 // for Vs 5.0 -> 2.75v(@zero pressure)  = 22000 * ADS115_MULTIPLIER 
#define MPX7002DP_ERROR_THRESHOLD 20
//#endif

/*
* Pressure sensors configurations
*/
#define SPYRO_KSYSTEM           110 // Ksystem assumed for spyro
#define FLOWRATE_MIN_THRESHOLD  4.0
#define CALIBRATION_COUNT       20

String sensorId2String(sensor_e type) {
  switch (type) {
  case SENSOR_PRESSURE_A0:
    return "SENSOR_PRESSURE_A0";
  case SENSOR_PRESSURE_A1:
    return "SENSOR_PRESSURE_A1";
  case SENSOR_DP_A0:
    return "SENSOR_DP_A0      ";
  case SENSOR_DP_A1:
    return "SENSOR_DP_A1      ";      
  case SENSOR_O2:
    return "SENSOR_O2         "; 
  default:
    return "WRONG SENSOR TYPE";
  }        
}

/*
* Initialization routine to setup the sensors
* Calibrate the sensors for errors
*/
int pressure_sensor::init() {
  int err = 0;
  delay(20); //delay of 20ms for sensors to come up and respond
  // Initialize the data
  this->m_data.current_data.pressure = 0.0;
  this->m_data.previous_data.pressure = 0.0;
  if(m_dp == 1) {
    this->m_data.actual_at_zero = MPX7002DP_ZERO_READING;
    this->m_data.error_threshold = MPX7002DP_ERROR_THRESHOLD;
  } else {
    this->m_data.actual_at_zero = MPX5010_ZERO_READING;
    this->m_data.error_threshold = MPX5010_ERROR_THRESHOLD;
  }
  this->m_data.error_at_zero = 0;
  //int needs 2 byes , so index multiplied by 2
  this->m_calibrationinpressure = retrieveCalibParam(EEPROM_CALIBRATION_STORE_ADDR*m_sensor_id*2);
  this->m_calibrationinpressure /= 1000;
  // EEPROM may be first time reading with 255 or -1 
  if ( 0 == this->m_calibrationinpressure) 
    this->m_calibrationinpressure = 0;
#if DEBUG_PRESSURE_SENSOR
    Serial.print("init(ID:");
    Serial.print(m_sensor_id);
    Serial.print(") ");
    Serial.print("ads:intPin :");
    Serial.print(m_ads->m_intPin);
    Serial.print(",ads:m_i2cAddress:");
    Serial.print(m_ads->m_i2cAddress);
    Serial.print(",adc_channel:");
    Serial.print(m_adc_channel);
    Serial.print(",m_dp:");
    Serial.println(m_dp);
    Serial.print("read m_calibrationinpressure*100 ");
    Serial.println(this->m_calibrationinpressure*1000);
#endif
  // Calibrate the sensors
#if 0
  err = sensor_zero_calibration();
  if(err) {
    Serial.print("ERROR: init failed for:");
    Serial.println(m_sensor_id);
    return err;
  } else {
    Serial.print("init done for:");
    Serial.println(m_sensor_id);
  }
#endif
  return 0;
}

/*
* Function to be called from timer interrupt to read
* and update the samples in local data structures
*/
void pressure_sensor::capture_and_store(void) {
  m_sample_index = m_sample_index + 1;
  if(m_sample_index >= MAX_SENSOR_SAMPLES) {
    m_sample_index = 0;
  }
  if(m_dp == 1) {
      this->m_data.current_data.flowvolume += get_spyro_volume_MPX7002DP();
      this->samples[m_sample_index] = this->m_data.current_data.flowvolume;
    } else {
      this->m_data.current_data.pressure = get_pressure_MPX5010() - this->m_calibrationinpressure;
      this->samples[m_sample_index] = this->m_data.current_data.pressure;
    }
}

/*
* Function to reset the local data structures
*/
void pressure_sensor::reset_sensor_data(void) {
  for(int index = 0; index < MAX_SENSOR_SAMPLES; index++) {
    this->samples[index] = 99.99;
  }
  if(m_dp == 1) {
      this->m_data.current_data.flowvolume = this->m_data.current_data.flowvolume = 0;
    } else {
      this->m_data.current_data.pressure = this->m_data.current_data.pressure = 0;
    }
}

/*
* Function to read stored sensor data from the
* local data structres
*/
float pressure_sensor::read_sensor_data() {
if(m_dp == 1) {
    this->m_data.previous_data.flowvolume = this->m_data.current_data.flowvolume;
    return this->m_data.previous_data.flowvolume;
  } else {
    this->m_data.previous_data.pressure = this->m_data.current_data.pressure;
    return this->m_data.previous_data.pressure;
  }
}


/*
* Function to return the pressure from the sensor
* vout = vs(0.09P + 0.04) + 5%VFSS
* P = (vout - (0.005*VFSS) - (Vs*0.04))/(VS * 0.09)
*/
float pressure_sensor::get_pressure_MPX5010() {
  float pressure = 0.0;
  float vout = 0.0;
  int err = 0;

  err = ADS1115_ReadVoltageOverI2C(m_ads, m_adc_channel, m_data.actual_at_zero, m_data.error_at_zero, &vout);
  if(ERROR_I2C_TIMEOUT == err) {
    Serial.print("ERROR: Sensor read I2C timeout failure for:");
    Serial.println(sensorId2String(m_sensor_id));
    this->set_error(ERROR_SENSOR_READ);
    return 0.0;
  } else {
     this->set_error(SUCCESS);
  }
    m_raw_voltage = vout*1000;

  pressure = ((vout - (MPX5010_ACCURACY) - (MPX5010_VS * 0.04))/(MPX5010_VS * 0.09));
  // Error correction on the pressure, based on the H2O calibration
  pressure = ((pressure - 0.07)/0.09075);

#if DEBUG_PRESSURE_SENSOR
    if ((millis() - m_lastmpx50102UpdatedTime) > SENSOR_DISPLAY_REFRESH_TIME)
    {  
      m_lastmpx50102UpdatedTime = millis();
      Serial.print("sensorType->");
      Serial.print(sensorId2String(m_sensor_id));
      Serial.print("::"); 
      Serial.print("C");
      Serial.print(" ");
      Serial.print(m_adc_channel);
      Serial.print(", V");
      Serial.print(" ");
      Serial.print(vout, 4);
      Serial.print(" ");
      Serial.print(", P");
      Serial.print(" ");
      Serial.println((pressure - m_calibrationinpressure), 4);
    }
#endif
  m_value = pressure - m_calibrationinpressure;
  return pressure;
}

#if 0
/*
* Function to calculate sensor errors during boot
*/
int pressure_sensor::sensor_zero_calibration() {
  float sample = 0.0;
  float avg = 0.0;
  int err = 0;

  for(int index = 0; index < CALIBRATION_COUNT; index++) {
  err = ADS1115_ReadAvgSamplesOverI2C(m_ads, m_adc_channel, &sample);
  if(err) {
    Serial.print("ERROR: ADC i2c failure for SensorId:");
    Serial.println(m_sensor_id);
    return ERROR_SENSOR_CALIBRATION;
  } 
  avg += sample;
  delay(10);
  }
  //repeat again 
  sample = 0.0; avg = 0.0; err = 0;

  for(int index = 0; index < CALIBRATION_COUNT; index++) {
  err = ADS1115_ReadAvgSamplesOverI2C(m_ads, m_adc_channel, &sample);
  if(err) {
    Serial.print("ERROR: ADC i2c failure for SensorId:");
    Serial.println(m_sensor_id);
    return ERROR_SENSOR_CALIBRATION;
  } 
  avg += sample;
  delay(10);
  }

   Serial.print("read voltage for SensorId:");
   Serial.print(m_sensor_id);
   Serial.print("  V:");
   Serial.println((avg/CALIBRATION_COUNT));
#if DEBUG_PRESSURE_SENSOR
      Serial.print("!!ADC i2c success for SensorId:");
      Serial.println(m_sensor_id);
#endif
  this->m_data.error_at_zero = ((avg/CALIBRATION_COUNT) - this->m_data.actual_at_zero);
  if(abs(this->m_data.error_at_zero) > this->m_data.error_threshold) {
    Serial.print("ERROR: calibration for SensorId:");
    Serial.print(m_sensor_id);
    Serial.print(" ,error_at_zero:");
    Serial.print(this->m_data.error_at_zero);
    Serial.print(" ,error_threshold:");
    Serial.println(this->m_data.error_threshold);
    return ERROR_SENSOR_CALIBRATION;
  } else {
#if DEBUG_PRESSURE_SENSOR
    Serial.print("calibration under threshold limits for:");
    Serial.println(m_sensor_id);
#endif
  }

#if DEBUG_PRESSURE_SENSOR
 {
    Serial.print("ZE");
    Serial.print(" ");
    Serial.println(this->m_data.error_at_zero);
  }
#endif
  return 0;
}
#endif

int pressure_sensor::sensor_zero_calibration() {
  float sample = 0.0;
  float avg = 0.0;
  int err = 0;
  float vout = 0.0;
  float pressure = 0.0;

  for(int index = 0; index < CALIBRATION_COUNT; index++) {
      err = ADS1115_ReadVoltageOverI2C(m_ads, m_adc_channel, m_data.actual_at_zero, m_data.error_at_zero, &vout);
  if(ERROR_I2C_TIMEOUT == err) {
    Serial.print("ERROR: Sensor read I2C timeout failure for:");
    Serial.println(sensorId2String(m_sensor_id));
    this->set_error(ERROR_SENSOR_READ);
    return 0.0;
  } else {
     this->set_error(SUCCESS);
  }

  if(m_dp)
    pressure += get_pressure_MPXV7002DP(vout);
  else
    pressure += get_pressure_MPX5010();
  }

  m_calibrationinpressure = pressure/CALIBRATION_COUNT;
  Serial.print("sensorType->");
  Serial.print(sensorId2String(m_sensor_id));
  Serial.print(", Correction in Pressure by ");
  Serial.println(m_calibrationinpressure);

  int store_param = (int)(m_calibrationinpressure * 1000);
  //eeprom needs 2 bytes , so *2 is added
  storeCalibParam(EEPROM_CALIBRATION_STORE_ADDR*m_sensor_id*2,store_param);
  Serial.print("store m_calibrationinpressure  ");
  Serial.println(store_param);

  return 0;
}



/*
* Function to get the flow rate of spyro
*/
float pressure_sensor::get_spyro_volume_MPX7002DP() {
  float vout = 0.0;
  float pressure = 0.0;
  float flowrate = 0.0, accflow = 0.0;
  int err = 0;

  err = ADS1115_ReadVoltageOverI2C(m_ads, m_adc_channel, m_data.actual_at_zero, m_data.error_at_zero, &vout);
  if(ERROR_I2C_TIMEOUT == err) {
    Serial.print("ERROR: Sensor read I2C timeout failure for:");
    Serial.println(sensorId2String(m_sensor_id));
    this->set_error(ERROR_SENSOR_READ);
    return 0.0;
  } else {
     this->set_error(SUCCESS);
  }
  
  m_raw_voltage = vout*1000;
  pressure = get_pressure_MPXV7002DP(vout);
  //add the correction done with calibration
  pressure -= m_calibrationinpressure;
  m_value = pressure;
  flowrate = get_flowrate_spyro(pressure);
  if(flowrate > FLOWRATE_MIN_THRESHOLD) {
    accflow = (((flowrate * 1000)/60000) * _aquisitionTimeMs);
  }

#if DEBUG_PRESSURE_SENSOR
  if ((millis() - m_lastmpx7002UpdatedTime) > SENSOR_DISPLAY_REFRESH_TIME)
    {
      m_lastmpx7002UpdatedTime = millis();
      Serial.print("sensorType->");
      Serial.print(sensorId2String(m_sensor_id));
      Serial.print("::");
      Serial.print("C");
      Serial.print(" ");
      Serial.print(m_adc_channel);
      Serial.print(", V*1000");
      Serial.print(" ");
      Serial.print(vout * 1000, 6);
      Serial.print(", P");
      Serial.print(" ");
      Serial.print(pressure, 6);
      Serial.print(", Cal");
      Serial.print(" ");
      Serial.print(m_calibrationinpressure, 6);
      Serial.print(", F");
      Serial.print(" ");
      Serial.print(flowrate, 6);
      Serial.print(", AF");
      Serial.print(" ");
      Serial.print(accflow, 6);
      if(m_dp == 1) {
        Serial.print(", Total AF");
        Serial.print(" ");
        Serial.println(this->m_data.current_data.flowvolume, 6);
      }
    }
#endif
  return accflow;
  }

/*
* Vout = Vs (0.2P + 0.5) +- accuracy%VFSS
* P = (Vout - accuracy*VFSS/100 - (Vs * 0.5))/(0.2 * Vs)
*/
float pressure_sensor::get_pressure_MPXV7002DP(float vout) {
  float tmppressure = 0.0;
  float pressure = 0.0;
  float correction = (MPXV7002DP_ACCURACY * MPXV7002DP_VFSS);
  pressure = (vout - correction - (MPXV7002DP_VS * 0.5))/(0.2 * MPXV7002DP_VS);
  pressure = ((m_lastPressure * 0.2) + (pressure * 0.8));
  tmppressure = pressure;
  m_lastPressure = pressure;
  if (tmppressure < 0)
    return tmppressure;
  else 
    return tmppressure;
}

/*
* Flowrate = Ksystem * sqrt(pressuredifference)
* API to return flow rate in liters per minute
*/
float pressure_sensor::get_flowrate_spyro(float pressure) {
  float flowrate = SPYRO_KSYSTEM * sqrt(abs(pressure));
  if(pressure > 0)
  return -flowrate;

  return flowrate;
}
