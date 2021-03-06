//******** Header Only Class for driving DRV8833 Dual Motor Driver and INA219 Current Sense x2 ***********//
/*
 * Current limiting attempts to keeps current below defined limit, resulting in Iave slightly below limit
 *  TODO: Saftey current limit motors in any mode to 300mA
 *        getcurrent open circuit error detection and handeling
 *        
 */
#ifndef MOT_CTRL_H
#define MOT_CTRL_H

#define DEBUG
// ONLY DEFINE THESE IF WANT TO USE ARDUINO SERIAL PLOTTER
// #define DEBUG_PLOT_A   
// #define DEBUG_PLOT_B
bool debug_called = false;

#include "INA219.h"		// includes i2c library
#include "LP_filter.h"

// torque/current limit vars
#define Default_curr_lim 60.0                   // BEST @ ~60mA, tested on 25mm geared motors
#define PWM__Curr_slew 1                     // max PWM inc/dec change for current limiting
#define curr_bound 0.85                         // lower bound relative to desired current at which to inc current
#define Samp_Freq 500                           // frequency at which to run current control loop   

// speed/voltage control vars
#define PWM_Drive_slew 3                        // slew PWM increment out of 255 (8bit pwm)
#define slew_freq 50                            // slew rate [hz], (50hz = 20ms)
#define Default_speed 255                       // 8-bit PWM

#define Mot_Polarity -1                         // rotation direction in which to drive motor
#define debug_rate 10                           //hz, print rate

// hardware pins
#define DRV_Sleep 7                             // sleep pin for motor driver

class  Motor
{
  private:
    // slew vars
    unsigned long last_slew_time;             

    // current limit vars  
    int8_t direct = 0;                        // commanded roll direction
    unsigned long last_CL_time;
    byte last_PWM = 0;
    LP_filter filter;

    
	  // drive vars              
	  int pin1;                                       
	  int pin2;
    enum{
      fwd=1,
      rev=-1,
    };

    //debug vars
    unsigned long last_debug_time;
  
  public:
	  char type;
    bool debug_enable;

    // FLAGS/STATES 
    bool drive_fwd = false;
    bool drive_rev = false;
    bool limit_curr = false;                  // state

    // current limiting vars
    Adafruit_INA219 ina_;
    float current_limit; 

  
	  Motor(char type_, uint8_t addr, const int pin1_, const int pin2_, float current_lim_ = Default_curr_lim)
	  {
  		type=type_;
  		pin1=pin1_;
  		pin2=pin2_;
      current_limit = current_lim_; 
      
  		ina_.setAddr(addr);                     //setup INA219 current sensor I2C address, Note: INA219.h is modified to allow this
	  }

	  void setup(){
  		// Initialize the INA219 current sensor and set calibration for 0.?mA resolution and 400mA, 16V maximum
  		ina_.begin();
  		ina_.setCalibration_16V_400mA_11bit();
  //    ina_.setCalibration_16V_400mA();
  
      
  		pinMode(pin1, OUTPUT);
  		pinMode(pin2, OUTPUT);
  		pinMode(DRV_Sleep, OUTPUT); 
	  }

	  void drive(byte speed, int8_t dir, bool limit_current){
  		speed = (byte)constrain(speed, 0, 255);
      
  		if (dir == fwd){
        analogWrite(pin1, speed);
        analogWrite(pin2,0);   
  		}
  		else if (dir == rev){
        analogWrite(pin2, speed);
        analogWrite(pin1,0);
  		}
      
//  		last_PWM = speed;
  		direct = dir;
  		limit_curr = limit_current;
	  }

    void ctrl_loop(){
      if ( !limit_curr && direct!=0){
        slew_drive();
      } else if (limit_curr){
        curr_limit();
      }
    }

	  void stop(){
  		analogWrite(pin1, 0);
  		analogWrite(pin2,0);
  
  		last_PWM = 0;
  		direct = 0;
  		limit_curr = false;
	  }

	  /**
	   * @param
	   * 
	   * @returns current in mA otherwise 2^10 if error 
	   * (currently no use in detecting open circuit) disabled for now... only valid on non-current limited motor)
	   */
	  float Get_Current(){
	//    float shmV = ina_.getShuntVoltage_mV();   
	//    if (shmV > 0.3 || shmV < -0.3 ){      
	//      return filter.filt(ina_.getCurrent_mA());
	//    } 
	//    else 
	//      return 1024;

		  return filter.slow(ina_.getCurrent_mA());
	  }

	/**
	 * @brief limits current within a range defined by current limit and current limit lower bound
	 * 
	 */
	  void curr_limit(){

		if ( micros() > last_CL_time + int(1.0/(float)Samp_Freq*1000000)){
  //      Serial.println(micros() - last_CL_time);                                   // check actual delay, if meeting freq
  		  last_CL_time = micros();
  		  float curr = Get_Current(); // note: curr is signed, when reverse, current flows '-'... ect.
  	      //Serial.println(direct*curr);
        
        if ( direct*curr > current_limit && curr != 1024.0){
          last_PWM -= PWM__Curr_slew;
          drive(last_PWM, direct, limit_curr);
        } else if( direct*curr <  current_limit*0.85 ){
          last_PWM += PWM__Curr_slew;
          drive(last_PWM, direct, limit_curr);
        } else {
        // inside current range, maintain PWM by doing nothing
        }
        
  		}
    
	  }
   
  /**
   * @brief limits PWM ramp rate for inrush current minimizing, and jerkiness reduction
   * 
   */
    void slew_drive(){
      if ( millis() > last_slew_time + int(1.0/(float)slew_freq*1000)){
        //      Serial.println(millis() - last_slew_time);                                   // check actual delay, if meeting freq
        last_slew_time = millis();

        if (last_PWM <= Default_speed - PWM_Drive_slew || last_PWM >= PWM_Drive_slew){
          last_PWM = (byte)constrain(Default_speed, last_PWM - PWM_Drive_slew, last_PWM + PWM_Drive_slew);
        } else if ( last_PWM > Default_speed - PWM_Drive_slew){
          last_PWM = Default_speed;
        } else {
          last_PWM = 0;
        }
        drive(last_PWM, direct, limit_curr);
      }
    }
   

	  void debug(){   
  		if ( millis() > last_debug_time + int(1.0/(float)debug_rate*1000)){
  		  last_debug_time = millis();
  		  float shuntvoltage = 0;
  		  float busvoltage = 0;
  		  float current_mA = 0;
  		  float loadvoltage = 0;
  		  float power_mW = 0;
  
  
      // every get is demanding, comment if not needed
  //		  shuntvoltage = ina_.getShuntVoltage_mV();
  //		  busvoltage = ina_.getBusVoltage_V();
  		  current_mA = ina_.getCurrent_mA();
  //		  power_mW = ina_.getPower_mW();
  //		  loadvoltage = busvoltage + (shuntvoltage / 1000);
  
         
  	//      Serial.print(busvoltage); 
  	//      Serial.print(" ");
  	//      Serial.print(shuntvoltage); 
  	//      Serial.print(" ");
  	//      Serial.print(loadvoltage); 
  	//      Serial.print(" ");
  	//      Serial.println(current_mA); 
  	//      Serial.print(" ");
  	//      Serial.println(power_mW);
        
  		  // comparing raw current vs processed current
  	//      Serial.print(shuntvoltage); 
  	//      Serial.print(" ");
  		  Serial.print(last_PWM); 
  		  Serial.print(" ");
        Serial.print(current_mA); 
        Serial.print(" ");
  		  Serial.println(Get_Current());
  
        
    
  	  //    delay(int(1.0/(float)Samp_Freq*1000));
  		//  Serial.print("Bus Voltage:   "); Serial.print(busvoltage); Serial.println(" V");
  		//  Serial.print("Shunt Voltage: "); Serial.print(shuntvoltage); Serial.println(" mV");
  		//  Serial.print("Load Voltage:  "); Serial.print(loadvoltage); Serial.println(" V");
  		//  Serial.print("Current:       "); Serial.print(current_mA); Serial.println(" mA");
  		//  Serial.print("Power:         "); Serial.print(power_mW); Serial.println(" mW");
  		//  Serial.println("");
  		//  delay(2000);
      
  		}
	  }

};

#endif
