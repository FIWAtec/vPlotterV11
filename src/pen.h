
#ifndef PEN_H
#define PEN_H

#include <ESP32Servo.h>

#define PEN_SERVO_PIN     32   
#define PEN_START_POS     80    

class Pen {
private:
    Servo *penServo;
    int    penDistance;         
    int    slowSpeedDegPerSec;  
    int    currentPosition;    

public:
    Pen();

    void setRawValue(int rawValue);  
    void setPenDistance(int value); 
    void slowUp();                  
    void slowDown();                
    bool isDown();                   
    int  currentAngle() const;      
};

#endif
