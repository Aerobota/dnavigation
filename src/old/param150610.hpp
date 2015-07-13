#ifndef __NAV_PARAM_H
#define __NAV_PARAM_H

const double DIST_LIMIT = 1.0;

// Waypoint
// 角度777が終了の合図 
                                                                            
const int wp_total_num = 7;
const double ox  = -0.683; // offset_x 
const double oy  = -1.973; // offset_y

enum  Robot_State { START_STATE, WP1_STATE, WP2_STATE, WP3_STATE, WP3_AGAIN_STATE,
		    WP4_STATE, NAVI_STATE, MOVE_PERSON_STATE, MOVE_OBJECT_STATE,
		    FOLLOW_STATE, GOAL_STATE };

                                                             
double waypoint[wp_total_num+2][3] = {
  // x, y, theta               
  //{ox+4.0,  oy+0.0, 90}, {ox+4.5,  oy+20.5,   0}, {ox+12.0, oy+20.5, 270},       
  //{ox+10.3, oy-0.6,  0}, {ox+24.2, oy-2.1, 270}, {ox+24.2, oy -4.7, 270},          
  //{ox+0.0, oy+0.0, 777}}                                                           
  // demulab inside  map: ~/tmp/demulab1.yaml                                        
  {0, 0, 0}, // dummy ウェイポイントの番号と配列の番号を同じにするため
  //{14.0, -26.7,  2.68}, // スタート地点
  { 7.7, -23.0, 2.68},  // 1
  {0.83, -19.0, 1.10},  // 2
  { 7.3, -20.0, -2.00}, // 3
  {13.0, -26.7,  1.10}, // 4
  {15.0, -26.7, -0.47}, // Goal
  {0, 0, 777} // dummy 終了用}
} ;
#endif
