#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "E101.h"

const int INITIAL_QUADRANT = 4; //Use this to skip quadrants when testing.

								//Sensors and motor constants
const int F_SENSOR = 6; //Front sensor pin
const int L_SENSOR = 5; //Left sensor pin
const int R_SENSOR = 0; //Right sensor pin
const int L_MOTOR = 2; //Left motor
const int R_MOTOR = 1; //Right motor

					   //Duty cycle constants
					   /*According to comments found on documentation, using 255 may
					   cause the H-bridge to get stuck, so MAX_DUTY_CYCLE is set to 254 instead. */
const int MAX_DUTY_CYCLE = 254; // Motor uses 100% capacity
const int MIN_DUTY_CYCLE = 30;  // Actually might be even lower than that.
const int BASE_DUTY_CYCLE = 40;  //Duty cycle when error is zero.

								 //Error calculation constants
const int KP = 30; //BASE_DUTY_CYCLE + KP cannot go past 254.
const int KD = 0;
const int KPQ4 = 12;
const int KDQ4 = 0;

//Image processing constants
const int PIC_WIDTH = 320;
const int PIC_HEIGHT = 240;
const int ROW = PIC_HEIGHT - 50;
const int ROW_AHEAD = 50;  //Used when we need to scan a row ahead of the ROW value.
const int MIN_H_TRACK_WID = 30;  //Mininum number of white pixels for a track in a horizontal scan.
const int MIN_V_TRACK_WID = 35;  //Mininum number of white pixels for a track in a vertical scan.
const int TRANSVERSAL = 290; //Mininum number of white pixels for a transversal line.
const int PASSAGE = 170; //Mininum number of white pixels for a left or right passage.
const int RED = 0;
const int GREEN = 1;
const int BLUE = 2;
const int LUM = 3;   //Luminosity = (red value + green value + blue value)/3
const int BASE_LUM_THRESH = 105;
const bool AUTO_THRESHOLD = false; //If true, calculates luminosity threshold automatically before starting.
const int MAX_BLK_NOISE = 5;   //Max. number of consecutive black pixels inside a track.
const int RED_THRESHOLD = 135; //Value for which the component of a pixel will be considered red.
const int GREEN_THRESHOLD = 100; //Value for which the component of a pixel will be considered green.
const int BLUE_THRESHOLD = 100; //Value for which the component of a pixel will be considered blue. 
const int MIN_RED_COUNTER = 70;  //Number of reddish pixels a line must have to be considered red.

								 //Distance control constants
const int MIN_DISTANCE = 210; //#todo Test and find a minimum distance to use in Q4. 250 is approx. 10cm.
const int TURN_TIME_SEC = 1;
const int TURN_TIME_MSEC = 500000;
const int TURN_TIME_TOT = 1500000; //Microseconds

								   //Gates and Network constants
char       PLEASE[] = "Please";        //If set to "const", the compiler will complain...
char       IP[] = "130.195.6.196"; //If set to "const", the compiler will complain...
const int  PORT = 1024;
const int  GATE_TIMER = 1500000; //Microseconds
const int  GATE2_DISTANCE = 160; //#todo find out what value to use.

								 //Fields
int    lum_threshold;
long   previous_time;  //Used to calculate Kd.
double previous_error; //Used to calculate Kd.

					   //Structure to store error data about tracks after image analysis. 
struct ImageData {
	int    total_white_pixels;
	double error1;       //For one track
	int    white_pixels1; //White pixels for one track
	double error2;       //For a possible second track
	int    white_pixels2; //White pixels for a possible second track
};

//Structure to store information about distance sensor readings.
struct Readings {
	double average;
	int    max;
	int    min;
};

//Establishes a threshold for the luminosity based on the minimum and maximum values
//of pixels in a picture taken by the robot, or uses the BASE_LUM_THRESHOLD.
void setLumThreshold() {
	lum_threshold = BASE_LUM_THRESH;
	if (AUTO_THRESHOLD) {
		take_picture();
		int min = 255;
		int max = 0;
		for (int x = 0; x <PIC_WIDTH; x++) {
			int luminosity = get_pixel(ROW, x, LUM);
			if (luminosity > max)
				max = luminosity;
			if (luminosity < min)
				min = luminosity;
		}
		lum_threshold = (int)(min + max) / 2;
	}
}

//Reads left digital sensor and returns true if an obstacle is close.
bool leftWall() {
	bool is_close = false;
	int  digital_reading = read_digital(L_SENSOR);
	if (digital_reading == 0)
		is_close = true;
	return is_close;
}

//Reads right digital sensor and returns true if an obstacle is close.
bool rightWall() {
	bool is_close = false;
	int  digital_reading = read_digital(R_SENSOR);
	if (digital_reading == 0)
		is_close = true;
	return is_close;
}

//Reads the given analog sensor a number of times and returns the average, max and min reading.
Readings readAnalogSensor(int sensor, int number_of_readings) {
	//Use F_SENSOR, L_SENSOR or R_SENSOR for sensor.
	//number_of_readings = 0, no reading is done at all
	//number_of_readings = 1, just one reading, i.e., no average
	//number_of_readings > 1, calculates the average.
	int    adc_reading = 0;
	int    max_reading = 0;
	int    min_reading = 2000;
	double average_reading = 0;

	for (int i = 0; i < number_of_readings; i++) {
		adc_reading = read_analog(sensor);
		average_reading = (i*average_reading + adc_reading) / (i + 1); //calculates average as it goes.
		if (adc_reading > max_reading) {
			max_reading = adc_reading;
		}
		if (adc_reading < min_reading) {
			min_reading = adc_reading;
		}
	}
	Readings results = { average_reading, max_reading, min_reading };
	return results;
}

//Analyses a picture horizontally and returns corresponding error signals and number of white pixels.
ImageData getHorizontalData(int y) {
	//y is the vertical coordinate of the row to be analyzed in the picture.
	//Pixels in the LEFT side of the image are assigned NEGATIVE values.
	//Pixels in the RIGHT side of the image are assigned POSITIVE values.
	int    pixel_value = -PIC_WIDTH / 2; //Value of the first pixel.
	int    track_number = 0;
	double error[] = { 0,0 }; //error[0] for first track detected, error[1] for second.
	int    total_white_pixels = 0;
	int    white_counter[] = { 0,0 };
	int    black_counter = 0;
	double noise_correction = 0; //To account for small number of black pixels inside a track.
	for (int x = 0; x < PIC_WIDTH; x++) {
		int luminosity = get_pixel(y, x, LUM); //Gets luminosity (whiteness) of pixel.
		if (luminosity > lum_threshold) {
			//Pixel is assumed to be white.
			white_counter[track_number]++;
			total_white_pixels++;
			error[track_number] += pixel_value;
			if (black_counter > 0) {
				int lum1 = get_pixel(y, x - 1, LUM);
				int lum2 = get_pixel(y, x - 2, LUM);
				if (lum1 > lum_threshold && lum2 > lum_threshold) {
					//Noise detected by the presence of black pixels inside the track
					//is only incorporated if previous two pictures are also white.
					error[track_number] += noise_correction;
					white_counter[track_number] += black_counter;
					black_counter = 0;
					noise_correction = 0;
				}
			}
		}
		else if (white_counter[track_number] > 0) {
			//Pixel is assumed to be black and there is a chance it is inside a white track.
			black_counter++;
			noise_correction += pixel_value;
			if (black_counter > MAX_BLK_NOISE) {
				//The amount of black pixels is too big, so the current region being analyzed
				//is assumed to either not be a track or be the right end of a track.
				black_counter = 0;
				noise_correction = 0;
				if (white_counter[track_number] < MIN_H_TRACK_WID) {
					//It is not a track: reset error and counter.
					error[track_number] = 0;
					white_counter[track_number] = 0;
				}
				else {
					//It is the right end of a track.
					//Start getting error for a possible second track in the picture
					track_number++;
				}
			}
		}
		pixel_value++;
		if (pixel_value == 0) {
			//When pixel_value gets to zero, it means the loop has reached the right
			//side of the image, so the first pixel_value must be adjusted to 1.
			pixel_value = 1;
		}
	}
	//Dividing the error by the number of white pixels normalizes its value
	//to account for different widths in the white lines. In fact, it makes the
	//error assume the pixel value correspondent to middle of the line.
	if (white_counter[0] > 0)
		error[0] = error[0] / white_counter[0];
	if (white_counter[1] > 0)
		error[1] = error[1] / white_counter[1];
	ImageData results = { total_white_pixels, error[0], white_counter[0], error[1], white_counter[1] };
	return results;
}

//Returns number of white pixels in a vertical scan.
int verticalWhitePix(int x) {
	int v_white_counter = 0;
	for (int y = 0; y < PIC_HEIGHT; y++) {
		int luminosity = get_pixel(y, x, LUM); //Gets luminosity (whiteness) of pixel.
		if (luminosity > lum_threshold)
			v_white_counter++;
	}
	return v_white_counter;
}

//Checks if there is a red line in the picture.
bool isRedLine() {
	bool result = false;
	int  red_counter = 0;
	for (int x = 0; x < PIC_WIDTH; x++) {
		int red_value = get_pixel(ROW, x, RED);
		int green_value = get_pixel(ROW, x, GREEN);
		int blue_value = get_pixel(ROW, x, BLUE);
		if (red_value > RED_THRESHOLD &&
			green_value < GREEN_THRESHOLD &&
			blue_value < BLUE_THRESHOLD)
			red_counter++;
	}
	if (red_counter >= MIN_RED_COUNTER)
		result = true;
	return result;
}

//Follows a white track according to the error provided.
void followTrack(ImageData image_data) {
	//Still need to implement derivative and determine value for KD.
	double error_percentage = image_data.error1 / (PIC_WIDTH / 2.0);
	double derivative = 0;
	double duty_cycle_correction = error_percentage*KP + derivative*KD;

	set_motor(L_MOTOR, BASE_DUTY_CYCLE + (int)duty_cycle_correction); //Final duty cycle must be an int.
	set_motor(R_MOTOR, BASE_DUTY_CYCLE - (int)duty_cycle_correction);
}

//Controls the position of the robot in the walled maze.
void q4Control(double current_state) {
	//Still need to implement derivative and determine value for KD.
	//current_state values:
	//    -100 <= current_state  < 0    Robot is far from left wall.
	//            current_state == 0    Robot is in the middle of the path.
	//       0 <  current_state <= 100  Robot is far from right wall.

	double error_percentage = current_state / 100.0;

	//long   time             = get current time...
	double error_variation = 1.0;//*(error_percentage - previous_error)/(time - previous_time);
								 //previous_time           = time;
								 //previous_error          = error

	double duty_cycle_correction = error_percentage*KPQ4 + error_variation*KDQ4;

	set_motor(L_MOTOR, BASE_DUTY_CYCLE + (int)duty_cycle_correction); //Final duty cycle must be an int.
	set_motor(R_MOTOR, BASE_DUTY_CYCLE - (int)duty_cycle_correction);
}

//==== Main =======================================================================================
int main() {
	int       quad = INITIAL_QUADRANT; //Flag to change quadrants.
	int       front_reading;
	ImageData h_data;
	ImageData previous_h_data;

	init();
	select_IO(L_SENSOR, 1); //Sets digital sensor channel to input mode.
	select_IO(R_SENSOR, 1);

	setLumThreshold();

	//==== QUADRANT 1&2 ===========================================================================
	while (quad == 1 || quad == 2) {
		//Quadrant 1 and 2
		//Goal: open gate and follow single track until Quad 3
		front_reading = readAnalogSensor(F_SENSOR, 1).average;

		if (front_reading>MIN_DISTANCE || quad == 1) {
			//Avoid collisions.
			set_motor(L_MOTOR, 0);
			set_motor(R_MOTOR, 0);
			if (quad == 1) {
				char message[24];
				connect_to_server(IP, PORT);
				send_to_server(PLEASE);
				receive_from_server(message);
				send_to_server(message);
				quad = 2;
				usleep(GATE_TIMER);
			}
		}
		else {
			take_picture(); //Take a picture and loads it to the memory.
			h_data = getHorizontalData(ROW);

			if (h_data.total_white_pixels >= TRANSVERSAL) {
				//Found a transversal track.
				//Robot is reaching Quadrant 3; the loop bellow controls the transition.
				//Slow down
				set_motor(L_MOTOR, (int)BASE_DUTY_CYCLE*0.7);
				set_motor(R_MOTOR, (int)BASE_DUTY_CYCLE*0.7);
				while (h_data.total_white_pixels >= TRANSVERSAL) {
					//Gets error of a region ahead of the transversal
					previous_h_data = h_data;
					h_data = getHorizontalData(ROW_AHEAD);
					if (h_data.white_pixels1 >= MIN_H_TRACK_WID) {
						followTrack(h_data);
						previous_h_data = h_data;
					}
					else {
						//Didn't find a track ahead.
						//This is very unlikely in Q2, it might have missed the first transversal.
						set_motor(L_MOTOR, (int)BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)-BASE_DUTY_CYCLE);
					}
					take_picture();
					h_data = getHorizontalData(ROW);
				}
				quad = 3; //Flag to start Q3
			}
			else if (h_data.white_pixels1 >= MIN_H_TRACK_WID) {
				//Follow the detected track.
				followTrack(h_data);
				previous_h_data = h_data;
			}
			else {
				//Lost track; must use data from previous picture to find it.
				set_motor(L_MOTOR, 0);
				set_motor(R_MOTOR, 0);
				while (h_data.white_pixels1 < MIN_H_TRACK_WID) {
					if (previous_h_data.error1 < 0) {
						//Track was on the left side before it was lost.
						set_motor(L_MOTOR, (int)-BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)BASE_DUTY_CYCLE);
					}
					else if (previous_h_data.error1 > 0) {
						//Track was on the right side before it was lost.
						set_motor(L_MOTOR, (int)BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)-BASE_DUTY_CYCLE);
					}
					else {
						//Inconclusive and highly unlike to happen, go back.
						set_motor(L_MOTOR, (int)-BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)-BASE_DUTY_CYCLE);
					}
					take_picture();
					h_data = getHorizontalData(ROW);
				}
				//Found a track.
				followTrack(h_data);
				previous_h_data = h_data;
			}
		}
	}

	//==== QUADRANT 3 =============================================================================
	bool red_line;
	while (quad == 3) {
		//Goal: finish the maze of white tracks.
		front_reading = readAnalogSensor(F_SENSOR, 1).average;

		if (front_reading>MIN_DISTANCE) {
			//Avoid collisions.
			set_motor(L_MOTOR, 0);
			set_motor(R_MOTOR, 0);
		}
		else {
			take_picture(); //Take a picture and loads it to the memory.

			red_line = isRedLine();
			if (red_line && (leftWall() || rightWall())) {
				//Robot is reaching Quadrant 4.
				while (red_line) {
					//Wait for it to cross the red line before switching to quad 4 loop.
					if (leftWall() && rightWall())
						q4Control(0);
					else if (leftWall() && !rightWall())
						q4Control(50);
					else if (!leftWall() && rightWall())
						q4Control(-50);
					else
						q4Control(0);
					red_line = isRedLine();
				}
				quad = 4;
				break;
			}

			h_data = getHorizontalData(ROW);

			if (h_data.total_white_pixels >= TRANSVERSAL) {
				//This is a transversal track
				//The best option in this case is always to take the path to the left

				while (h_data.white_pixels1 >= TRANSVERSAL) {
					//Advances until losing sight of transversal.
					set_motor(L_MOTOR, (int)BASE_DUTY_CYCLE*0.7);
					set_motor(R_MOTOR, (int)BASE_DUTY_CYCLE*0.7);
					take_picture();
					h_data = getHorizontalData(ROW);
					previous_h_data = h_data;
				}

				//Tries to get track slightly ahead.
				h_data = getHorizontalData(ROW - 20);
				while (h_data.white_pixels1 < MIN_H_TRACK_WID) {
					//Turns left until finding a new track
					set_motor(L_MOTOR, (int)-BASE_DUTY_CYCLE);
					set_motor(R_MOTOR, (int)BASE_DUTY_CYCLE);
					take_picture();
					h_data = getHorizontalData(ROW - 20);
					usleep(100000);
				}
				followTrack(h_data);
				previous_h_data = h_data;
			}
			else if (h_data.total_white_pixels >= PASSAGE) {
				//Tries to get track ahead.
				previous_h_data = h_data;
				h_data = getHorizontalData(ROW_AHEAD);
				if (h_data.white_pixels1 >= MIN_H_TRACK_WID) {
					followTrack(h_data);
					previous_h_data = h_data;
				}
				else {
					set_motor(L_MOTOR, 0);
					set_motor(R_MOTOR, 0);
					int pix_on_left = verticalWhitePix(0);
					int pix_on_right = verticalWhitePix(PIC_WIDTH - 1);
					if (pix_on_right >= MIN_V_TRACK_WID && pix_on_left >= MIN_V_TRACK_WID) {
						set_motor(L_MOTOR, (int)-BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)BASE_DUTY_CYCLE);
						usleep(100000);
					}
					if (pix_on_right >= MIN_V_TRACK_WID) {
						set_motor(L_MOTOR, (int)BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)-BASE_DUTY_CYCLE);
						//usleep(75000);
					}
					else if (pix_on_left >= MIN_V_TRACK_WID) {
						set_motor(L_MOTOR, (int)-BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)BASE_DUTY_CYCLE);
						//usleep(75000);
					}
				}
			}
			else if (h_data.white_pixels1 >= MIN_H_TRACK_WID) {
				//Follow the detected track.
				followTrack(h_data);
				previous_h_data = h_data;
			}
			else { //h_data.white_pixels1 < MIN_H_TRACK_WID
				set_motor(L_MOTOR, 0);
				set_motor(R_MOTOR, 0);

				int pix_on_left = verticalWhitePix(0);
				int pix_on_right = verticalWhitePix(PIC_WIDTH - 1);

				while (h_data.white_pixels1 < MIN_H_TRACK_WID || abs(h_data.error1) > 100) {
					//Turns to some direction until finding a track and having it on the central area of the image.
					//Note: try different values for the second condition if robot is turning past the track or stops turning before it is centered.

					//If robot turns to the wrong side in a corner, there might be a problem with the results from verticalWhitePix().
					//With the camera too close to the ground, the images are too "zoomed" and vertical scans become more unreliable.
					//Try removing the block of ifs that relies on vertical scans if it is not working well.
					if (previous_h_data.error1 > 0 && pix_on_left < MIN_V_TRACK_WID && pix_on_right >= MIN_V_TRACK_WID) {
						//Guaranteed to be on the right.
						set_motor(L_MOTOR, (int)BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)-BASE_DUTY_CYCLE);
						//usleep(75000);
					}
					else if (previous_h_data.error1 < 0 && pix_on_left >= MIN_V_TRACK_WID && pix_on_right < MIN_V_TRACK_WID) {
						//Guaranteed to be on the left.
						set_motor(L_MOTOR, (int)-BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)BASE_DUTY_CYCLE);
						//usleep(75000);
					}
					else if (previous_h_data.error1 < 0 && pix_on_left < MIN_V_TRACK_WID && pix_on_right >= MIN_V_TRACK_WID) {
						//Likely to be on the right.
						set_motor(L_MOTOR, (int)BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)-BASE_DUTY_CYCLE);
						//usleep(75000);
					}
					else if (previous_h_data.error1 > 0 && pix_on_left >= MIN_V_TRACK_WID && pix_on_right < MIN_V_TRACK_WID) {
						//Likely to be on the left.
						set_motor(L_MOTOR, (int)-BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)BASE_DUTY_CYCLE);
						//usleep(75000);
					}
					//Try this if the robot is turning to the wrong direction on the last transversal.
					else if (pix_on_left >= MIN_V_TRACK_WID && pix_on_right >= MIN_V_TRACK_WID) {
						//Possibly the second transversal.
						set_motor(L_MOTOR, (int)-BASE_DUTY_CYCLE);
						set_motor(R_MOTOR, (int)BASE_DUTY_CYCLE);
						//usleep(75000);
					}
					//This part doesn't rely on vertical scans. Try using only this on Monday.
					else {
						//Ideally, it shouldn't come to this in corners: the vertical scans should be able to
						//tell the direction to follow...
						//It's hard to tell which way to go in this case, it will follow previous_h_data.
						if (previous_h_data.error1 < 0) {
							//Track was on the left side before it was lost.
							set_motor(L_MOTOR, (int)-BASE_DUTY_CYCLE);
							set_motor(R_MOTOR, (int)BASE_DUTY_CYCLE);
						}
						else if (previous_h_data.error1 > 0) {
							//Track was on the right side before it was lost.
							set_motor(L_MOTOR, (int)BASE_DUTY_CYCLE);
							set_motor(R_MOTOR, (int)-BASE_DUTY_CYCLE);
						}
						else {
							//Inconclusive, go back. Highly unlikely to happen.
							set_motor(L_MOTOR, (int)-BASE_DUTY_CYCLE);
							set_motor(R_MOTOR, (int)-BASE_DUTY_CYCLE);
						}
					}
					take_picture();
					h_data = getHorizontalData(ROW);
				}
				//Back on track, supposedly...
				followTrack(h_data);
				previous_h_data = h_data;
			}
		}
	}

	//==== QUADRANT 4 =============================================================================
	while (quad == 4) {
		//Quadrant 4
		//Goal: finish the walled maze.
		int min_distance = 210; //Change depending on the algorithm being tested.

		take_picture();
		if (isRedLine()) {
			set_motor(L_MOTOR, 0);
			set_motor(R_MOTOR, 0);
			
			while (readAnalogSensor(F_SENSOR, 5).average < GATE2_DISTANCE) {
				set_motor(L_MOTOR, 0);
				set_motor(R_MOTOR, 0);
			}
			while (readAnalogSensor(F_SENSOR, 5).average >= GATE2_DISTANCE) {
				set_motor(L_MOTOR, 0);
				set_motor(R_MOTOR, 0);
				if(readAnalogSensor(F_SENSOR, 5).average < GATE2_DISTANCE){
					set_motor(L_MOTOR, 30);
					set_motor(R_MOTOR, 30);
					}
			}
			usleep(GATE_TIMER); //Wait just a little more to avoid a collision with a partially open gate.
		}

		front_reading = read_analog(F_SENSOR);
		if (front_reading < min_distance) {
			//No wall ahead. Advance.
			if (leftWall() && rightWall()) {
				//Sets error to zero. Kd would be a good thing to avoid sudden changes...
				q4Control(0);
			}
			else if (!leftWall() && !rightWall()) {
				//Sets error a bit to the right to make robot follow the right wall in passages.
				q4Control(25);
			}
			else if (leftWall() && !rightWall()) {
				q4Control(50);
			}
			else if (!leftWall() && rightWall()) {
				q4Control(-50);
			}
		}
		else {
			//Front wall is close.

			////==== Q4 Algorithm 2 ===============================================================================
			set_motor(L_MOTOR,0);
			set_motor(R_MOTOR,0);
			
			////The internal distance corresponds to the minimum distance that allows the robot to
			////safely make the turns, and the bigger distance is the value that guarantees the robot
			////is not seeing walls ahead of it, i.e., it completed the turn.
			int bigger_distance   = 160;
			int internal_distance = 200; //Set as min_distance <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
			bool right = true;
			if (leftWall() && !rightWall()){
			//    //Turns to the right until front sensor stops detecting walls.
				right = true;
			    while (front_reading > bigger_distance){ //#todo find a good value for this bigger distance
			        int left_dc  = MIN_DUTY_CYCLE; //#todo check if it is better to rotate
			        int right_dc = 0;              //#todo check if it is better to rotate
			        if (front_reading < internal_distance){
			            left_dc  += 25; //#todo test
			            right_dc += 25; //#todo test
			//            //Perhaps you should add a value that is proportional to the
			//            //difference in the distance? i.e., proportional to (current reading - internal_distance)
			        }
			        set_motor(L_MOTOR,left_dc);
			        set_motor(R_MOTOR,right_dc);
			        front_reading = read_analog(F_SENSOR);
			    }

			}
			else if (!leftWall() && rightWall()){
			//    //Turns to the left until front sensor stops detecting walls.
				right = false;
			    while (front_reading > bigger_distance){ //#todo find a good value for this bigger distance
			        int left_dc  = 0; //#todo check if it is better to rotate
			        int right_dc = MIN_DUTY_CYCLE;              //#todo check if it is better to rotate
			        if (front_reading < internal_distance){
			            left_dc  += 25; //#todo test
			            right_dc += 25; //#todo test
			//            //Perhaps you should add a value that is proportional to the
			//            //difference in the distance? i.e., proportional to (current reading - internal_distance)
			        } 
			        set_motor(L_MOTOR,left_dc);
			        set_motor(R_MOTOR,right_dc);
			        front_reading = read_analog(F_SENSOR);
			    }
			}
			else {
			    while (front_reading > min_distance){
			        if(leftWall() && rightWall()){
			//            //Slowly go back
			            set_motor(L_MOTOR,-BASE_DUTY_CYCLE);
			            set_motor(R_MOTOR,-BASE_DUTY_CYCLE);
			        }
			        else if (!leftWall() && !rightWall()){
			//            //Slowly go back
			            set_motor(L_MOTOR,-BASE_DUTY_CYCLE);
			            set_motor(R_MOTOR,-BASE_DUTY_CYCLE);
						if (right == true){
							while (front_reading > bigger_distance) { //#todo find a good value for this bigger distance
								int left_dc = MIN_DUTY_CYCLE; //#todo check if it is better to rotate
								int right_dc = 0;              //#todo check if it is better to rotate
								if (front_reading < internal_distance) {
									left_dc += 25; //#todo test
									right_dc += 25; //#todo test
													//            //Perhaps you should add a value that is proportional to the
													//            //difference in the distance? i.e., proportional to (current reading - internal_distance)
								}
								set_motor(L_MOTOR, left_dc);
								set_motor(R_MOTOR, right_dc);
								front_reading = read_analog(F_SENSOR);
							}
						}
						else if(right == false){
							while (front_reading > bigger_distance) { //#todo find a good value for this bigger distance
								int left_dc = 0; //#todo check if it is better to rotate
								int right_dc = MIN_DUTY_CYCLE;              //#todo check if it is better to rotate
								if (front_reading < internal_distance) {
									left_dc += 25; //#todo test
									right_dc += 25; //#todo test
													//            //Perhaps you should add a value that is proportional to the
													//            //difference in the distance? i.e., proportional to (current reading - internal_distance)
								}
								set_motor(L_MOTOR, left_dc);
								set_motor(R_MOTOR, right_dc);
								front_reading = read_analog(F_SENSOR);
							}
						}
			        }
			        front_reading = read_analog(F_SENSOR);
			    }
			}
			




		}
	}

}
