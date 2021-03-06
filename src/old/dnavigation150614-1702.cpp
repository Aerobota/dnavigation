// RoboCup@Home follow me program by demura.net
#include "dnavigation.hpp"
#include "param.hpp"

const char *beep  = "/usr/share/xemacs21/xemacs-packages/etc/sounds/piano-beep.wav";
// http://pocket-se.info/ ポケットサウンドの効果音利用 heal3.mp3をwavに変換                                       
const char *chime = "/home/demura/myprog/src/voice/heal3.wav";


template <typename T> std::string toString(const T& t)
{
  std::ostringstream os; os<<t; return os.str();
}

void sleepok(int t, ros::NodeHandle &nh) {
  if (nh.ok()) sleep(t);
}

// 単純移動平均フィルタ　Simple Moving Average Filter
double Robot::SMAfilter(double value, double *data, const int length)
{
  int    count = 0;
  double ave   = 0;

  data[0]  = value;
  for (int i=0; i < length; i++) {
    if (data[i] == 999) { 
      continue;
    }
    else {
      ave += data[i]; 
      count++;
    }
  }

  if (count != 0) {
    ave /= count;
  }
  else {
    ave = 999;
  }

  for (int i=length-2;i >= 0; i--) {
    data[i+1] = data[i];
  } 

  return ave;
}


// コンストラクタ
Object::Object()
{
  last_distance = 0;
  last_angle    = 0;
  last_local.x  = 0;
  last_local.y  = 0;
  local.x       = 0;
  local.y       = 0;
  local.velocity = 0;
  intensity_min  = 0;   // min is 0;
  intensity_max  = 255; // max is 255
  
}


// コンストラクタ
Robot::Robot()
{
  linear_speed  = 0;
  angular_speed = 0;
  last_x        = 0;
  last_y        = 0;
  last_theta    = 0;
  last_time     = 0;
  x             = 0;
  y             = 0;
  state         = START_STATE; 
  theta         = 0;
  time          = 0;

  human_lost    = true;
  recog_kenseiko = false;
  recog_mini     = false;
  recog_followme = false;
  recog_leave    = false;
  recog_stop     = false;
  reached_wp4    = false;
  second_section = false;
  waypoint_num   = 1;
}

bool Robot::checkDoorOpen()
{
  int center = dataCount/2;
  int search_lines = 1080 *  (15.0/270.0); // 走査線数 30 deg
  double angle,danger_radius = 0.5;  // [m]
  double distance = 1.5;

  int j = 0;
  for (int i = center - search_lines/2; i <= center + search_lines/2; i++) {
    angle = j * 1.5 * M_PI/1080;  // 270度で1080本
    j++;

    double x = laser_distance[i] * cos(angle);
    double y = laser_distance[i] * sin(angle);

    // distance以内に障害物あり                                                              
    if ((fabs(x) <= danger_radius) && (fabs(y) <= distance)) {
      return false;
    }
  }
  return true;
}

//　ロボットのほぼ正面の障害物までの距離を返す
double Robot::measureObstacleDist(double degree)
{
  int center = dataCount/2;
  int search_lines = 1080 *  (degree/270.0); // 走査線数 30 deg                                                                    
  double angle, min_dist;
  
  int j = 0;
  for (int i = center - search_lines/2; i <= center + search_lines/2; i++) {
    angle = j * 1.5 * M_PI/1080;  // 270度で1080本                                                                              
    j++;
    double x = laser_distance[i] * cos(angle);
    double y = laser_distance[i] * sin(angle);
    double distance = sqrt(x*x + y*y);
    
    if (distance < min_dist) {
      min_dist = distance;
    }
  }
  return min_dist;
}


bool Robot::choosePathMarco() {
  int center = dataCount/2;
  int search_lines = 1080 * (180.0/270.0);
  double leftSum = 0;
  double rightSum = 0;
  for (int i = center - search_lines/2; i <= center; i++) {
    if (laser_distance[i] != 999) {
      leftSum += laser_distance[i];
    }
  }
  for (int i = center; i <= center + search_lines/2; i++) {
    if (laser_distance[i] != 999) {
      rightSum += laser_distance[i];
    }
  }
  if (leftSum > rightSum) {
    return true;
  }
  else {
    return false;
  }
}

void Robot::setLinearSpeed(double _linear)
{
  linear_speed = _linear;
}

void Robot::setAngularSpeed(double _angular)
{
  angular_speed = _angular;
}

void Robot::setPose(double tx, double ty, double th)
{
  x = tx;
  y = ty;
  theta = th;
}

void Robot::move()
{
  geometry_msgs::Twist cmd;
  cmd.linear.x  = linear_speed;
  cmd.angular.z = angular_speed;
  cmdVelPublisher.publish(cmd);
}

void Robot::move(double _linear, double _angular)
{
  geometry_msgs::Twist cmd;
  cmd.linear.x  = _linear;
  cmd.angular.z = _angular;
  cmdVelPublisher.publish(cmd);
}

void Robot::setReachedWP4()
{
  reached_wp4 = true;
}


void Robot::stop()
{
  geometry_msgs::Twist cmd;
  cmd.linear.x  = 0;
  cmd.angular.z = 0;
  cmdVelPublisher.publish(cmd);
}

// test function
void Robot::test()
{
  turn(0.5*M_PI);
}

// LIDARデータを画像に変換する
void Robot::changeToPicture(int dataCount, double laser_angle_min,
               double laser_angle_max, double laser_angle_increment)
{
  static int64 epochs = 0; 

  lidar_gray_image = cv::Scalar::all(0); // 画面を黒くする

  int center = dataCount/2;  // レーザーの中央の番号                                 
  double init_search_dist  = 2.0; // 追跡する人の初期距離 [m]                        
  int search_lines = 1080 *  (follow_angle/270.0); // 走査線数


  for (int j = center - search_lines/2; j <= center + search_lines/2; j++) {
  //for (int j = center - 50; j <= center + 50; j++) {
    // 動いている物体だけ追跡するために、範囲を絞る
    // 自分も動いているので、静止物体を検出するために以下を実行する
    // (1) 回転した角度分に相当する走査線番号を戻る 
    // (2) 移動した分に相当する距離を進める
    //double time_diff = laser_time - laser_last_time;

    double move_diff 
      = sqrt((getX() - getLastX()) * (getX() - getLastX())
      + (getY() - getLastY()) * (getY() - getLastY()));
    double angle_diff = RAD2DEG(getTheta() - getLastTheta());
    int    line_diff; 
    line_diff = (int) (angle_diff * 1080 / 270.0);

    if (j - line_diff < center - search_lines/2) continue; // 計測範囲外
    if (j - line_diff > center + search_lines/2) continue; // 計測範囲外
    double dist_diff = laser_distance[j-line_diff] - (laser_last_distance[j] + move_diff);
    double speed;
    double time_diff = getTime() - getLastTime();  
    if (time_diff != 0) speed = dist_diff/ time_diff;
    else                speed = 0;

    //for (int j = 0; j <= dataCount; j++) {
    int x=0, y=0, tmp=0;
    //if (laser_distance[j] <= 0.5*IMAGE_WIDTH*sqrt(2)/mToPixel) {
    double angle = (laser_angle_max - laser_angle_min) * j
      / (double) dataCount + laser_angle_min;
    x = mToPixel * laser_distance[j]*cos(angle) + (int) (0.5 * IMAGE_WIDTH);
    y = mToPixel * laser_distance[j]*sin(angle) + (int) (0.5 * IMAGE_HEIGHT);
    tmp = x;
    x   = lidar_image.cols- y;   // x軸も左右反転 画面は左隅が0,0
    y   = lidar_image.rows - tmp; // y軸は上下反転
    //}

    //cout << "intensity=" << laser_intensities[j] << endl;
    //std::cout << " x="<< x << " y=" << y << std::endl;
    if ((0 <= x) && (x < lidar_image.cols) && (0 <= y) && (y < lidar_image.rows)) {
      // fast version
      int value = (int) (laser_intensities[j] * 255.0/6000.0);
      if (value > 255)  value = 255;
      if (value <   0)  value =   0;
      lidar_gray_image.data[y*lidar_gray_image.step+x*lidar_gray_image.elemSize()] 
	= value; 
      //cout << "intensities[" << j << "]=" << value << endl;
      // slow version
      //lidar_image.at<cv::Vec3b>(y,x) = cv::Vec3b(255,0,0);
    }
  }
}


void Robot::laserCallback(const sensor_msgs::LaserScan laser_scan)
{
  double laser_time, laser_diff_time; //  [s] 
  static double laser_last_time;

  int dataNum = 0;

  //ROS_INFO("size[%d]: ", laser_scan.ranges.size());
  //ROS_INFO("angle min=%.2f max=%.2f inc=%f\n",laser_scan.angle_min,
  //	   laser_scan.angle_max, laser_scan.angle_increment);
  //ROS_INFO("range min=%.2f max=%.2f \n",laser_scan.range_min,
  //         laser_scan.range_max);
  //int64 time = cv::getTickCount();

  dataCount = laser_scan.ranges.size();
  laser_angle_min = laser_scan.angle_min;
  laser_angle_max = laser_scan.angle_max;
  
  for(int i = 0; i < dataCount; i++) {       
    double value = laser_scan.ranges[i];
    //ROS_INFO("value[%d]:%f\n", i,value);
    if ((value >= laser_scan.range_min) && (value <= laser_scan.range_max))
    {
      laser_distance[i] = value;
      laser_intensities[i] = laser_scan.intensities[i];
    }
    else {
      laser_distance[i] = 999; // invalid data
      laser_intensities[i] = -999;
    }
  }
  //cout << "laser_scan.range_min=" << laser_scan.range_min <<  " max=" << laser_scan.range_max << endl;
  //cout << "data count=" << laser_scan.ranges.size() << endl;
  //exit(1);

  laser_time = cv::getTickCount();
  double f = 1000.0/(cv::getTickFrequency());
  laser_diff_time = laser_time - laser_last_time;

  setTime((double) cv::getTickCount()/ cv::getTickFrequency());

  changeToPicture(dataCount, laser_scan.angle_min,
            laser_scan.angle_max,laser_scan.angle_increment);

  setLastX(getX());
  setLastY(getY());
  setLastTheta(getTheta());
  setLastTime(getTime());

  for(int i = 0; i < dataCount; i++) {
    laser_last_distance[i] = laser_distance[i];
    laser_last_intensities[i] = laser_intensities[i];
  }
  //printf("Time diff=%f[ms]\n", (cv::getTickCount()-time)*1000/cv::getTickFrequency());  
  //static double time_old;
  //double time2  = (double) cv::getTickCount()*1000.0/cv::getTickFrequency();
  //printf("Diff Time =%f[ms]\n", time2);
  //printf("last_diff_time =%f[ms]\n", laser_diff_time);
  laser_last_time  = laser_time;
}

// Bubble sortをして中央値を返す
double Robot::median(int no, double *data)
{
  for (int i=0; i < no-1; i++) {
    for (int j=no-1; j > i; j--) {
      if (data[j] < data[j-1]) {
	double tmp = data[j-1];
	data[j-1] = data[j];
	data[j] = tmp;
      }
    }
  }
  return data[(no+1)/2];
}


// Memorize the operator
void Robot::memorizeIntensity()
{
  const int repeat_no = 20;
  int center = dataCount/2;
  int search_lines = 1080 *  (20.0/270.0); // 走査線数  


  // 足を肩幅程度に開きロボットの前約１ｍに立ってもらう
  printf("**** Stand 1 meter ahead from me ****\n");
  printf("center=%d\n",center);
  //system(SPEAK_REMEMBER);

  sc->say("Stand 1.0  meter ahead from me");
  sleep(3);

  char str[128];

  double tmin = 50, tdist=1.0;
  int tcount = 0;
  
  while (!((tdist-0.1 < tmin) && (tmin < tdist+0.1)) && (tcount < 3)) {
    tmin = 50;
    for (int i = center - search_lines/2; i <= center + search_lines/2; i++) {
      if (tmin > laser_distance[i])  tmin = laser_distance[i];
    }
    sprintf(str,"%.2fmeter",tmin);
    printf("%.2f\n",tmin);
    sc->say(str);
    sleep(3);
    if ((tdist-0.1 < tmin) && (tmin < tdist+0.1)) tcount++;
    else tcount = 0;

    
    if (tdist+0.1 < tmin)      sc->say("close to me");
    else if (tmin < tdist-0.1) sc->say("apart from me");
    else sc->say("OK");

    sleep(2);
    ros::spinOnce();
  }

  if (system(SPEAK_REMEMBER) == -1) printf("system error\n");

  // 最小値、最大値、平均値、標準偏差を求める
  const double stand_distance_min  = 0.4;
  const double stand_distance_max  = 2.0;
  double min[repeat_no], max[repeat_no], ave[repeat_no], sigma[repeat_no];

  for (int k = 0; k < repeat_no; k++) {
    double sum = 0, sum2 = 0, data[1081];
    min[k] = 6000;
    max[k] = 0;

    int count = 0;
    for (int i = center - search_lines/2; i <= center + search_lines/2; i++) {
      if ((stand_distance_min <= laser_distance[i])
	  && (laser_distance[i] <= stand_distance_max)) {
	data[count++]  = laser_intensities[i];
	sum           += laser_intensities[i];
	if ((0 <  laser_intensities[i]) && (laser_intensities[i] < min[k]))
	  min[k] = laser_intensities[i];
	if ((max[k] < laser_intensities[i] && laser_intensities[i] <6000))
	  max[k] = laser_intensities[i];
	//printf("No.%3d: distance= %.2f intensity=%.1f \n",
	//     i, laser_distance[i],laser_intensities[i]);


      }
    }
    if (count != 0) ave[k] = sum/count;
    else            ave[k] = 0;
    
    for (int j =0; j < count; j++) {
      sum2   += pow(data[j]-ave[k],2);
    }
    if (count != 0) sigma[k] = sqrt(sum2)/count;
    else            sigma[k] = 0;


    if (count < 50) k--; // if the number of data is less than 50, do it again
    else {
      printf("epoch=%2d line no=%d Intensity min=%.1f  max=%.1f ave=%.2f sigma=%.2f\n",
           k,count,min[k]*255.0/6000.0, max[k]*255.0/6000.0, ave[k]*255.0/6000.0, sigma[k]*255.0/6000.0);
    }
    ros::spinOnce();
    usleep(50*1000); // 0.1[s]
  }

  // 中央値の計算
  double min_med   = median(repeat_no, min);
  double max_med   = median(repeat_no, max);
  double ave_med   = median(repeat_no, ave);
  double sigma_med = median(repeat_no, sigma);

  printf("*** Finished recognition ***\n");
  printf("*** Intensity min=%.1f  max=%.1f ave=%.2f sigma=%.2f ***\n",
         min_med*255.0/6000, max_med*255.0/6000, 
	 ave_med*255.0/6000, sigma_med*255.0/6000);
  human.intensity_min = min_med;
  human.intensity_max = max_med;
  min_med = min_med * 255.0/6000.0;
  max_med = max_med * 255.0/6000.0;
  ave_med = ave_med * 255.0/6000.0;

  // ズボンが正規分布と仮定し、平均から+-３標準偏差(99.7%)の範囲をしきい値とする
  min_med = ave_med - 5.0 * sigma_med * 255.0/6000;
  max_med = ave_med + 5.0 * sigma_med * 255.0/6000;

  human.intensity_min = (int) min_med;
  human.intensity_max = (int) max_med;
  printf("*** human Intensity min=%d  max=%d *** \n",
         human.intensity_min, human.intensity_max);
  if (system(SPEAK_REMEMBERED)== -1) printf("system error\n");
}

void Robot::recognizeVoice(const string str)
{
  const char *beep  = "/usr/share/xemacs21/xemacs-packages/etc/sounds/piano-beep.wav";
  // http://pocket-se.info/ ポケットサウンドの効果音利用 heal3.mp3をwavに変換  
  const char *chime = "/home/demura/myprog/src/voice/heal3.wav";

  int retry = 0, retry_no = 2;

  ros::spinOnce();

  while (recog_mini == false) {
    //sc->startWave(beep);
    sc->say("Say, Mini");
    //int val = system(SPEAK_KENSEIKO);
    sleep(2);
    ros::spinOnce();
    if (retry++ >= retry_no) break;
    if (recog_kenseiko == true) sc->startWave(chime);
  }
  sleep(2);

  bool recog;

  //sc->say(str);                                                                          
  retry = 0;
  recog = false;

  while (recog == false) {
    cout << "recog:" << recog << endl;
    //ut << "state:" << state << endl;
    //sc->startWave(beep);                                                                              
    ros::spinOnce(); // これを忘れるとcallbackしない                                       

    //if (state == STATE1) int val1 = system(SPEAK_SAY_FOLLOWME);
    //if (state == STATE2) int val2 = system(SPEAK_SAY_LEAVE);
    //system(SPEAK_STOP);
    sc->say("say, stop");
    sleep(4);

    switch (state) {
      //case STATE1: recog = recog_followme; break;
      //case STATE2: recog = recog_leave; break;
    }
    if (recog == true) sc->startWave(chime);

    //sc->say(str);
    if (retry++ >= retry_no) break;
  }
  //sc->say(str);
  //sc->startWave(chime);
  sleep(2);
  //if (state == STATE1) int val1 = system(SPEAK_FOLLOWYOU);
  //if (state == STATE2) int val2 = system(SPEAK_LEAVE);
  //sleep(2);
}


// 音声認識がうまくいかないときのデモ用
void Robot::recognizeSimpleVoice(const string str)
{
  bool flag_loop1 = false;
  bool flag_loop2 = false;

  const char *beep = "/usr/share/xemacs21/xemacs-packages/etc/sounds/piano-beep.wav";
  // http://pocket-se.info/ ポケットサウンドの効果音利用 heal3.mp3をwavに変換  
  const char *chime = "/home/demura/myprog/src/voice/heal3.wav";

  ros::spinOnce();
  //sc->say("Say, Kenseiko chan");
  if (system(SPEAK_KENSEIKO) == -1) printf("system error\nm");
  usleep(6000*1000);
  if (recog_kenseiko == true) {
    sc->startWave(chime);
  }
  else {
    sc->startWave(beep);
  }

  usleep(3000*1000);
  sc->say(str);
  usleep(6000*1000);

  bool recog;
  if (recog == true) {
    sc->startWave(chime);
  }
  else {
    sc->startWave(beep);
  }

  usleep(3000*1000);
}



void Robot::memorizeOperator()
{
  /* char key;
  printf("Memorize intensity [y] or use default value [n]:");
  //cin >> key;
  //cout << "key:" << key << endl;
  key = 'n';
  int val = system(SPEAK_STAND);
  sleep(3);

  if (key == 'y') {
    std::cout << "memorize intensity" << std::endl;
    memorizeIntensity();

  }
  else {
    std::cout << "Use default value" << std::endl;
    human.intensity_min =  40; // 50;
    human.intensity_max = 190; // 170
  }

  #ifdef SPEECH
  //const char *str2 = "/usr/share/xemacs21/xemacs-packages/etc/sounds/piano-beep.wav";
  // http://pocket-se.info/ ポケットサウンドの効果音利用 heal3.mp3をwavに変換

  const string str= "Say, follow me";
  //recognizeSimpleVoice(str);
  //state = STATE1;
  recognizeVoice(str);
  //system(SPEAK_FOLLOWYOU);
  sleep(2);
  #endif
  */
  human.intensity_min =  40;
  human.intensity_max = 190;
}

// ローカル座標系をワールド座標系へ変換
void Robot::localToWorld(Pose local_pose, Pose *world_pose )
{
  //double rx = kobuki->getPoseX();  // ROSは進行方向がx, 左方向がy軸
  //double ry = kobuki->getPoseY();
  //double rtheta = kobuki->getPoseTh();

  world_pose->x = local_pose.x * cos(theta) - local_pose.y * sin(theta)
                + x;
  world_pose->y = local_pose.x * sin(theta) + local_pose.y * cos(theta)
                + y;
  world_pose->theta = theta;
}

int Robot::findLegs(cv::Mat input_image, Object *object, cv::Mat result_image,
	     cv::Mat display_image, const string& winname, cv::Scalar color, 
	     int contour_min, int contour_max, int width_min, int width_max,
	     double ratio_min, double  ratio_max,double m00_min, double m00_max, 
	     double m10_min, double m10_max, double diff_x,  double diff_y)
{
  static int epoch = 0;


  // Measure time begin
  int64 time = cv::getTickCount();

  std::vector<std::vector<cv::Point> > contours;
  std::vector<cv::Vec4i> hierarchy;

  findContours(input_image, contours, hierarchy, 
	       CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, cv::Point(0 , 0) );            
  //findContours(e2_img, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, cv::Point(0 , 0) );

  //cout << "countour number=" << contours.size() << endl;
  int object_num = 0;

  for(unsigned int cn=0; cn<contours.size(); cn++)
    {
      cv::Point2f center;
      float radius;
      double tmp, count = 0, intensity = 0;

      // 反射強度による除外
      /* for(int i=0; i<contours[cn].size();i++){
	tmp = lidar_gray_image.at<uchar>(contours[cn][i].y, contours[cn][i].x);
	//printf("(%d,%d)=%.1f\n",contours[cn][i].x,contours[cn][i].y,tmp);
	if (tmp != 0) {
	  count++;
	  intensity += tmp;
	}
      }
      if (count != 0) intensity /= count;
      else intensity = 0; 
      if (intensity < 200) continue; */

      // find minimum circle enclosing the contour
      cv::minEnclosingCircle(contours[cn],center,radius);

      // ロボットより後ろは除外                  
      if (center.y -IMAGE_HEIGHT/2 > 0) continue;

      // 輪郭の長さにより除外
      if (!((contours[cn].size() >= contour_min) && (contours[cn].size() <= contour_max))) continue;

      //  follow_max_distance * mToPixelより遠い物体は検出しない
      if (follow_max_distance * mToPixel < IMAGE_HEIGHT/2 - center.y) continue; 

      // 外接する長方形を求める
      cv::Rect rect = cv::boundingRect(cv::Mat(contours[cn]));

      // 長方形の底辺による除外
      if (!((rect.width >= width_min) && (rect.width <= width_max))) continue;

      // 縦横比による除外
      double ratio;
      if (rect.width != 0) {
	ratio = (double) rect.height/rect.width;
	if (!((ratio >= ratio_min) && (ratio <= ratio_max))) continue;
      }

      // 面積による除外(m00)
      cv::Moments mom = cv::moments(contours[cn]);
      if (!((mom.m00 >  m00_min) && (mom.m00 < m00_max))) continue;

      // m01
      //int m01_min = 0, m01_max = 40000;
      //if (!((mom.m01 > m01_min) && (mom.m01 < m01_max))) continue;

      // m10
      //int m10_min = 3400, m10_max = 25000;
      if (!((mom.m10 > m10_min) && (mom.m10 < m10_max))) continue;

      // 重心による判定
      // 脚（円柱）の断面はU字型なので重心のy座標が円より下になる
      // x座標は中心近辺。中心からずれている脚は追わない 
      //float y_thresh = 0.2;
      Point2f point;
      point.x = mom.m10/mom.m00;
      point.y = mom.m01/mom.m00;

      //if (fabs(center.x-point.x) > diff_x) continue;
      //if (rect.tl().y+rect.height/2 - point.y > diff_y) continue;

      if(center.y - point.y > diff_y)  continue;

      // 反射強度による除外  
      for (int i=rect.tl().y; i < rect.tl().y + rect.height; i++) { 
	for (int j=rect.tl().x; j < rect.tl().x + rect.width; j++) {
	  tmp = lidar_gray_image.at<uchar>(i, j);
	  if (tmp != 0) {
	    count++;
	    intensity += tmp;
	  }
	}
      }
      if (count != 0) intensity /= count;
      else intensity = 0;

      // 反射強度は距離の関数なので変更の必要あり
      //double intensity_min = 120, intensity_max = 125;  // チノパン
      // double intensity_min = 80, intensity_max = 160;   // 黒室内
      //double intensity_min = 140, intensity_max = 300;   // 茶色、家
      //human.intensity_min = 90; human.intensity_max = 159;  // コーデロイ　茶
     
      // 反射強度による除外
      if (!((intensity > human.intensity_min) && (intensity < human.intensity_max))) continue;  

      //if (intensity > 133 && intensity < 136) continue;  // 近い壁


      // 矩形をファイルに保存. データ保存用. for learning and Hu moment
      /* cv::Mat roi(display_image, cv::Rect(rect.tl().x, rect.tl().y, 
					  rect.width, rect.height)); 

      if ((center.x > 230) && (center.x < 270)) {
	static int img_no = 0;
	char file_name[10];
	sprintf(file_name, "img/img%d.pgm",img_no++);
	imwrite(file_name,roi);
	} */ 

      //cv::Mat roi = input_image(cv::Rect(rect.tl().x, rect.tl().y,          
      //					 rect.width, rect.height));

      // Hu モーメントによる識別
      // あまりうまくいっていない
      //cv::Rect roi2(rect.tl().x, rect.tl().y,rect.width, rect.height);
      //cv::Mat  gray_image = lidar_gray_image.clone();
      //cv::Mat  roi_img = gray_image(roi2);
      /* double value = 1000, value_min = 100;
      for (int i=0; i < template_number; i++) {
      //for (int i=0; i < 1; i++) {
	//printf("Channels=%d\n",img.channels());
	//cvtColor(img, gray_img,CV_RGB2GRAY);
	value = cv::matchShapes(contours[cn],template_contours[i][0], 
				 CV_CONTOURS_MATCH_I3, 0);

	if (value < value_min) value_min = value;
	//value = cv::matchShapes(contours[cn],template_contour0[1],
	//			CV_CONTOURS_MATCH_I2, 0);
	//imshow("tmp",img);
	//printf("contour=%d template=%d value=%f\n", cn, i, value);
	} */




      #ifdef MOMENT_EXEL
      double contour_size_array[1081], intensity_array[1081], rect_width_array[1081];
      double ratio_array[1081], m00_array[1081], m01_array[1081];
      double m10_array[1081], m11_array[1081];
      double diff_x_array[1081], diff_y_array[1081];
      double contour_size_sum = 0, intensity_sum = 0, rect_width_sum = 0;          
      double contour_size_min = 1000, contour_size_max = 0;  
      double ratio_sum = 0, m00_sum = 0,  m01_sum = 0, m10_sum = 0, m11_sum = 0;     
      double diff_x_sum =0, diff_y_sum = 0; 
      double ratio_min = 100, ratio_max = 0, m00_min = 1000, m00_max = 0;
      double m01_min = 100000, m01_max = 0, m10_min = 100000, m10_max = 0; 
      double m11_min = 1000000, m11_max = 0; 
      double diff_x_min =1000, diff_x_max = -1000;
      double diff_y_min = 1000, diff_y_max =-1000;    
      double rect_width_min = 500, rect_width_max = 0;  
      double intensity_min = 255, intensity_max = 0;

      if ((center.y > 50)   && (center.y < 250)) {
	if ((center.x > 230) && (center.x < 270)) {
	  printf("Epochs=,%d,", epoch);
	  printf("Contour[%d],(, %.0f, %.0f,),size=,%d,intensity=,%.1f, ",cn, center.x, center.y, (int) contours[cn].size(),intensity);
	  printf(" rect.width=,%d, ratio=,%.2f,",rect.width, ratio);
	  printf("  m00=,%.1f, m01=,%.1f, m10=,%.1f, m11=,%.1f, ",mom.m00, mom.m01, mom.m10, mom.m11);
	  printf("  center.x-point.x=,%f, center.y-point.y=,%f\n", center.x-point.x, rect.tl().y+rect.height/2-point.y);
	  
	  contour_size_array[epoch] = contours[cn].size();
	  contour_size_sum         += contours[cn].size();                 
	  if (contours[cn].size() < contour_size_min) contour_size_min = contours[cn].size();
	  if (contours[cn].size() > contour_size_max) contour_size_max =contours[cn].size();

	  intensity_array[epoch]    = intensity;
	  intensity_sum            += intensity;
	  if (intensity < intensity_min) intensity_min = intensity;
	  if (intensity > intensity_max) intensity_max = intensity;

	  rect_width_array[epoch]   = rect.width;
	  rect_width_sum           += rect.width;
	  if (rect.width < rect_width_min) rect_width_min = rect.width;
	  if (rect.width > rect_width_max) rect_width_max = rect.width;

	  ratio_array[epoch]        = ratio;
	  ratio_sum                += ratio;
	  if (ratio < ratio_min) ratio_min = ratio;
	  if (ratio > ratio_max) ratio_max = ratio;

	  m00_array[epoch]   = mom.m00;
	  m00_sum           += mom.m00;
	  if (mom.m00 < m00_min) m00_min = mom.m00;
	  if (mom.m00 > m00_max) m00_max = mom.m00;

	  m01_array[epoch]   = mom.m01;
	  m01_sum           += mom.m01;
	  if (mom.m01 < m01_min) m01_min = mom.m01;
          if (mom.m01 > m01_max) m01_max = mom.m01;

	  m10_array[epoch]   = mom.m10;
	  m10_sum           += mom.m10;
	  if (mom.m10 < m10_min) m10_min = mom.m10;
          if (mom.m10 > m10_max) m10_max = mom.m10;

	  m11_array[epoch]   = mom.m11;
	  m11_sum           += mom.m11;
	  if (mom.m11 < m11_min) m11_min = mom.m11;
          if (mom.m11 > m11_max) m10_max = mom.m11;

	  diff_x_array[epoch]  = center.x - point.x;
	  diff_x_sum          += center.x - point.x;
	  if (center.x - point.x < diff_x_min) diff_x_min = center.x - point.x;
	  if (center.x - point.x > diff_x_max) diff_x_max = center.x - point.x;

	  diff_y_array[epoch] = center.y - point.y;
	  diff_y_sum        += center.y - point.y;
	  if (center.y - point.y < diff_y_min) diff_y_min = center.y - point.y;
          if (center.y - point.y > diff_y_max) diff_y_max = center.y - point.y;
	  epoch++;
	}

	double epoch_max = 1000;

	if (epoch >= epoch_max-1) {
	  double contour_size_ave = 0, intensity_ave = 0, rect_width_ave = 0;
	  double ratio_ave = 0, m00_ave = 0, m01_ave = 0, m10_ave = 0, m11_ave = 0;
	  double diff_x_ave = 0, diff_y_ave = 0;

	  contour_size_ave = contour_size_sum/epoch_max;
	  intensity_ave    = intensity_sum/epoch_max;
	  rect_width_ave   = rect_width_sum/epoch_max;
	  ratio_ave        = ratio_sum/epoch_max;
	  m00_ave          = m00_sum/epoch_max;
	  m01_ave          = m01_sum/epoch_max;
	  m10_ave          = m10_sum/epoch_max;
	  m11_ave          = m11_sum/epoch_max;
	  diff_x_ave       = diff_x_sum/epoch_max;
	  diff_y_ave       = diff_y_sum/epoch_max;

	  double contour_size_sum2 = 0, intensity_sum2 = 0, rect_width_sum2 = 0;
	  double ratio_sum2 = 0, m00_sum2 = 0, m01_sum2 = 0, m10_sum2 = 0, m11_sum2 = 0;
	  double diff_x_sum2 = 0, diff_y_sum2 = 0;

	  for (int i=0; i < epoch_max; i++) {
	    contour_size_sum2 += pow(contour_size_array[i]-contour_size_ave,2);
	    intensity_sum2    += pow(intensity_array[i]-intensity_ave,2);
	    rect_width_sum2   += pow(rect_width_array[i]-rect_width_ave,2);
	    ratio_sum2        += pow(ratio_array[i]-ratio_ave,2);
	    m00_sum2          += pow(m00_array[i]-m00_ave,2);
	    m01_sum2          += pow(m01_array[i]-m01_ave,2);
	    m10_sum2          += pow(m10_array[i]-m10_ave,2);
	    m11_sum2          += pow(m11_array[i]-m11_ave,2);
	    diff_x_sum2       += pow(diff_x_array[i]-diff_x_ave,2);
	    diff_y_sum2       += pow(diff_y_array[i]-diff_y_ave,2);
	  }

	  double contour_size_sigma, intensity_sigma, rect_width_sigma, ratio_sigma;
	  double m00_sigma, m01_sigma, m10_sigma, m11_sigma, diff_x_sigma, diff_y_sigma;
	  contour_size_sigma = sqrt(contour_size_sum2)/epoch_max;
	  intensity_sigma    = sqrt(intensity_sum2)/epoch_max;
	  rect_width_sigma   = sqrt(rect_width_sum2)/epoch_max;
	  ratio_sigma        = sqrt(ratio_sum2)/epoch_max;
	  m00_sigma          = sqrt(m00_sum2)/epoch_max;
	  m01_sigma          = sqrt(m01_sum2)/epoch_max;
	  m10_sigma          = sqrt(m10_sum2)/epoch_max;
	  m11_sigma          = sqrt(m11_sum2)/epoch_max;
	  diff_x_sigma       = sqrt(diff_x_sum2)/epoch_max;
	  diff_y_sigma       = sqrt(diff_y_sum2)/epoch_max;

	  printf("*****************************************************\n");
	  printf("************ Statistical information  ***************\n");
	  printf("*****************************************************\n\n");
	  printf("contour size: min=%.0f max=%.0f ave=%.2f sigma=%.3f \n",
		 contour_size_min,contour_size_max,contour_size_ave,contour_size_sigma);
          printf("intensity: min=%.0f max=%.0f ave=%.2f sigma=%.3f \n",
                 intensity_min,intensity_max, intensity_ave, intensity_sigma);
	  printf("rect width: min=%.0f max=%.0f ave=%.2f sigma=%.3f \n",
                 rect_width_min,rect_width_max, rect_width_ave, rect_width_sigma);
	  printf("ratio: min=%.0f max=%.0f ave=%.2f sigma=%.3f \n",
                 ratio_min,ratio_max, ratio_ave, ratio_sigma);
	  printf("m00: min=%.0f max=%.0f ave=%.2f sigma=%.3f \n",
                 m00_min,m00_max, m00_ave, m00_sigma);
	  printf("m01: min=%.0f max=%.0f ave=%.2f sigma=%.3f \n",
                 m01_min,m01_max, m01_ave, m01_sigma);
	  printf("m10: min=%.0f max=%.0f ave=%.2f sigma=%.3f \n",
                 m10_min,m10_max, m10_ave, m10_sigma);
	  printf("m11: min=%.0f max=%.0f ave=%.2f sigma=%.3f \n",
                 m11_min,m11_max, m11_ave, m11_sigma);
	  printf("diff x: min=%.2f max=%.2f ave=%.2f sigma=%.3f \n",
                 diff_x_min,diff_x_max, diff_x_ave, diff_x_sigma);
	  printf("diff y: min=%.2f max=%.2f ave=%.2f sigma=%.3f \n",
                 diff_y_min,diff_y_max, diff_y_ave, diff_y_sigma);

	  exit(1);
	}
	
      }
      #endif

      //cv::circle(display_image,center,radius,color,1);
      cv::rectangle(display_image,rect,color,1);
      //std::vector<cv::Point> hull;
      ///cv::convexHull(cv::Mat(contours[cn]),hull);
      //cv::drawContours(result_image, hull, -1, cv::Scalar(0), 2);
      //cv::drawContours(display_image,hull,0,(255,0,0),2);
      //cv::cvCircle(display_image,center,radius,color,1);                          
      //cout << "no="<< cn << " cx=" << center.x << " cy=" << center.y << " my=" << point.y <<
      //  " diffy=" << point.y - center.y << " radius=" << radius  <<  endl;
      //cout << " diffx=" << point.x - center.x << endl;
      object[object_num].radius    = radius;
      object[object_num].image_pos = center;
      
      // ローカル座標系はROSに合わせて進行方向がx, 左方向がy
      object[object_num].local.y    = (center.x - IMAGE_WIDTH/2) / mToPixel;
      object[object_num].local.x    = (center.y - IMAGE_WIDTH/2) / mToPixel;
      localToWorld(object[object_num].local, &object[object_num].world);
      object[object_num].setX(object[object_num].world.x);
      object[object_num].setY(object[object_num].world.y);
      object[object_num].setTheta(object[object_num].world.theta);
      object_num++;
    }


  //printf("FindLeg Time=%f[ms]\n", (cv::getTickCount()-time)*1000/cv::getTickFrequency());
  //cout << "Object num=" << object_num << endl;
  //cv::imshow(winname, display_image);
  return object_num;

}


// 人の位置推定アルゴリズム(ローカル座標系)
void Robot::calcHumanPose(int object_num,  Object *object, Object *human_obj)
{
  // 人の位置推定アルゴリズム
  // 物体数０：ロスト    
  // 物体数１：ロスト
  // 物体数２以上：１番目と２番目に近い物体の中心。ただし、２つの重心が50cm以上離れていると除外する。  

  static int64 time2;

  double min1_dist = 999999999, min1_angle = 999, min1_num=999, image1_dist;
  double min2_dist = 999999999, min2_angle = 999, min2_num=999, image2_dist;
  Point2f min1_point, min2_point;

  switch (object_num) {
  case 0: 
  case 1: {
    human_obj->distance = 999;
    human_obj->angle    = 999;
    human_obj->local.x  = 999;
    human_obj->local.y  = 999;
    human_obj->last_local.x  = 999;
    human_obj->last_local.y  = 999;
    human_obj->local.velocity = 999;
    human_obj->setX(999);
    human_obj->setY(999);
    human_obj->setTheta(999);
    human_obj->image_pos.x = 999;
    human_obj->image_pos.y = 999;

    return;
  }
  default: {// 2個以上
    // 1番近い物体を探す
    for (int i=0; i < object_num; i++) {
      double diff1 = fabs(object[i].image_pos.x - IMAGE_WIDTH/2);
      if (diff1 == 0) diff1 = 0.01;
      // 左右６０度以内を脚と考える。それより外は追わない.
      if ((object[i].image_pos.y - IMAGE_HEIGHT/2 < 0)
	  && (object[i].image_pos.y - IMAGE_HEIGHT/2) / diff1 >= -0.5) continue; 
      image1_dist = (object[i].image_pos.x - IMAGE_WIDTH/2) * (object[i].image_pos.x -IMAGE_WIDTH/2)
	+ (object[i].image_pos.y - IMAGE_HEIGHT/2) * (object[i].image_pos.y -IMAGE_HEIGHT/2);
      if (image1_dist < min1_dist) {
	min1_num  = i;
	min1_dist = image1_dist;
	min1_point.x = object[i].image_pos.x;
	min1_point.y = object[i].image_pos.y;
      }
    }
    // ２番目に近い物体を探す 
    for (int i=0; i < object_num; i++) {
      if (i == min1_num) continue;
      double diff2 = fabs(object[i].image_pos.x - IMAGE_WIDTH/2);
      if (diff2 == 0) diff2 = 0.01;
      // 左右60度以内を脚と考える。それより外は追わない. 
      if ((object[i].image_pos.y - IMAGE_HEIGHT/2 < 0)
	  && (object[i].image_pos.y - IMAGE_HEIGHT/2) / diff2 >= -0.5) continue;
      
      image2_dist = pow(object[i].image_pos.x - IMAGE_WIDTH/ 2, 2)
	+ pow(object[i].image_pos.y - IMAGE_HEIGHT/2, 2);
      
      if (image2_dist < min2_dist) {
	min2_num  = i;
	min2_dist = image2_dist;
	min2_point.x = object[i].image_pos.x;
	min2_point.y = object[i].image_pos.y;
      }
    }
    
    // ２個の距離が0.6m以上離れていたら誤検出     
    double d2 = sqrt(pow(min1_point.x - min2_point.x, 2)
		+  pow(min1_point.y - min2_point.y, 2)) /mToPixel;
    if (d2 < 0.6) {
      human_obj->distance = (sqrt(min1_dist) + sqrt(min2_dist))/(mToPixel * 2);
      human_obj->angle  = ((atan2(min1_point.x - IMAGE_WIDTH/2, IMAGE_HEIGHT/2 - min1_point.y))
			   +(atan2(min2_point.x - IMAGE_WIDTH/2, IMAGE_HEIGHT/2 - min2_point.y)))/2;
      
      // 画像に円を表示
      cv::circle(lidar_image,min1_point,5,blue,1);
      cv::circle(lidar_image,min2_point,5,blue,1);
      
      double ave_x = (min1_point.x + min2_point.x)/2;
      double ave_y = (min1_point.y + min2_point.y)/2;
      // ローカル座標系はROSに合わせて進行方向がx, 左方向がy
      human_obj->local.y =   (ave_x - IMAGE_WIDTH/2) / mToPixel;
      // 画像座標系のy軸は下方向が正
      human_obj->local.x = - (ave_y - IMAGE_WIDTH/2) / mToPixel;
      
      double dt = (double) (cv::getTickCount()-time2)/(cv::getTickFrequency());
      
      if (dt == 0 || human_obj->last_distance == 999){
	human_obj->local.velocity = 999;
      }
      else {
	human_obj->local.velocity = (human_obj->distance - human_obj->last_distance)/dt;
      }
      time2 = cv::getTickCount();
      
      //cout << "human velocity=" << human_obj->local.velocity 
      //	   << " dist=" << human_obj->distance << " last_dist=" << human_obj->last_distance 
      //   << " dt=" << dt << endl;
      
      //human_obj->last_distance = human_obj->distance;
      human_obj->last_local.x = human_obj->local.x;
      human_obj->last_local.y = human_obj->local.y;

      Pose world_pose; 
      localToWorld(human_obj->local, &world_pose);
      human_obj->setX(world_pose.x);
      human_obj->setY(world_pose.y);
      human_obj->setTheta(world_pose.theta);
      human_obj->image_pos.x = ave_x;     // 画像座標系
      human_obj->image_pos.y = ave_y;
      
      //cv::circle(lidar_image,cv::Point(ave_x, ave_y),5,red,2); 
      return;
    }
    else {
      human_obj->distance = 999;
      human_obj->angle  = 999;
      human_obj->local.x = 999;
      human_obj->local.y = 999;
      human_obj->last_local.x = 999;
      human_obj->last_local.y = 999;
      human_obj->local.velocity = 999;
      human_obj->setX(999);
      human_obj->setY(999);
      human_obj->setTheta(999);
      human_obj->image_pos.x = 999;     // 画像座標系
      human_obj->image_pos.y = 999;

      //*dist = sqrt(min1_dist) / mToPixel;
      //*ang  = atan2(min1_point.x - IMAGE_WIDTH/2, IMAGE_HEIGHT/2 - min1_point.y);
      //cv::circle(lidar_image,min1_point,5,blue,2);
    }
    return;
  }
  }
}


// Read template files
void Robot::readTemplate()
{

  for (int i=0; i < template_number; i++) {
    char file_name[50];
    sprintf(file_name, "/home/demura/img/image%d.pgm",i);  
    cout << file_name << endl;
    templateFiles.push_back(file_name);
  }

  int j=0;
  template_contours.resize(template_number);
  std::vector<std::string>::iterator it = templateFiles.begin();
  for (; it != templateFiles.end(); ++it) {
    cv::Mat temp_bin_image;
    cv::Mat temp = cv::imread(*it,0);
    cv::threshold(temp, temp_bin_image, 0, 255,
		  cv::THRESH_BINARY| cv::THRESH_OTSU);
    cv::findContours(temp_bin_image,template_contours[j++], 
		     cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
  }

  /* cv::Mat temp0 = cv::imread("/home/demura/img/image0.pgm",0);
  printf("Channels=%d\n",temp0.channels());
  cv::Mat temp0_bin_image;
  cv::threshold(temp0, temp0_bin_image, 0, 255,
		cv::THRESH_BINARY| cv::THRESH_OTSU);

		cv::findContours(temp0_bin_image,template_contour0, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE); */
}


void Robot::welcomeMessage()
{
  std::cout << "Starting the Navigation Test" << std::endl;
  std::cout << "follow_max_distance =" << follow_max_distance << std::endl;
  std::cout << "follow_min_distance =" << follow_min_distance << std::endl;
  std::cout << "follow_angle        =" << follow_angle << std::endl;
  std::cout << "leg_width_max       =" << leg_width_max << std::endl;
  std::cout << "leg_width_min       =" << leg_width_min << std::endl;
}

void Robot::prepRecord()
{
  if(!writer1.isOpened()) {
    cout << "video file open error" << endl;
    exit(1);
  }
  if(!writer2.isOpened()) {
    cout << "video file open error" << endl;
    exit(1);
  }
}

void Robot::record(cv::VideoWriter writer, cv::Mat image)
{
  writer << image;
}

// 障害物チェック
// 前方distanceまでの障害物をチェックする
// 何もない場合はfalse、ある場合はtrueを返す
bool Robot::checkObstacles(double distance)
{
  int center = dataCount/2;
  int search_lines = 1080 *  (180.0/270.0); // 走査線数                                 
  double angle,danger_radius = 0.25; //0.25;  // [m]                                                   

  int j = 0;
  for (int i = center - search_lines/2; i <= center + search_lines/2; i++) {
    angle = j * 1.5 * M_PI/1080;  // 270度で1080本
    j++;

    double x = laser_distance[i] * cos(angle);
    double y = laser_distance[i] * sin(angle);

    // distance以内に障害物あり
    if ((fabs(x) <= danger_radius) && (fabs(y) <= distance)) {
      return true;
    }
  }
  return false;
}

// 状態チェック
// 0: 障害物なし、1:衝突回避圏内に障害物あり
// 119:危険領域に障害物あり。緊急事態
// 999: エレベータの中
int Robot::checkCondition(double theta)
{
  int center = dataCount/2;
  int search_lines = 1080 *  (180.0/270.0); // 走査線数                                 
  double angle = 0, x, y;
  double danger_radius   = 0.20;  // [m]                                                   
  double danger_distance = 0.25;  // [m]                
  double avoid_distance  = 0.60;  // [m] 衝突回避する直線距離

  //cout << "center=" << center << endl; 
  //cout << "search_lines/2=" << search_lines/2 << endl; 
  //cout << "angle_min=" << robotPtr->laser_angle_min << endl; // -2.35
  //cout << "angle_max=" << robotPtr->laser_angle_max << endl; //  2.35
  //cout << "theta=" << theta << endl;

  int j = 0;
  bool inElevator = true, danger = false;

  for (int i = center - search_lines/2; i <= center + search_lines/2; i++) {
    // エレベータ内部の判定
    double elevator_size = 1.5;
    if (theta == 0) {
      // １回でもelevator_sizeより大きければエレベータではない
      //cout << " No:" << i << " dist=" << laser_distance[i] << endl;
      if (laser_distance[i] > elevator_size) {
	inElevator = false;
      }
    }

    // 角度theta [rad]に相当する捜査線の数だけ足す（マイナスがあるから)
    angle = j * 1.5 * M_PI/1080;
    j++;

    int k = i + theta * 1080/(1.5*M_PI);
    if (k < center - search_lines/2) continue; // 測定外は飛ばす
    if (k > center + search_lines/2) continue;

    x = laser_distance[k] * cos(angle);
    y = laser_distance[k] * sin(angle);
    //cout << "theta=" << RAD2DEG(theta) << " i=" << i << " k=" << k << " x=" << fabs(x) << " y=" << fabs(y) << endl;

    // Avoid collision with peple
    //if (state == STATE3) danger_radius = 0.35;

    if ((fabs(x) <= danger_radius) && (fabs(y) <= danger_distance)) {
      // 衝突する可能性が極めて大。緊急事態
      danger = true;
      if (theta != 0) return DANGER;
      //cout << "distance=" << laser_distance[j] << endl;
    }
  }
  if (inElevator == true) return IN_ELEVATOR;
  else if (danger  == true) return DANGER;
  else return SAFE;
}

// 衝突検出   
// 0: 障害物なし、1:右側に障害物あり, -1:左側に障害物あり 
// 119:衝突圏内に障害物あり。緊急事態, 2:エレベーター内   
int Robot::checkCollision()
{
  //cout << "check collision" << endl;
  for (int i = 0; i < 90; i+= 5) {
    for (int j = -1; j < 2; j+=2) {
      int condition;
      condition = checkCondition(DEG2RAD((double) i *  j));

      if (condition == SAFE) return - i * j;
      else if (condition == DANGER) {
	//cout << "Danger" << endl;
	return DANGER;
      }
      else if (condition == IN_ELEVATOR) {
	//cout << "In elevator" << endl;
	return IN_ELEVATOR;
      }
    }
  }
}

void Robot::goAhead(double distance)
{
  double dist, last_dist, diff_dist = 0;
  double x,y, last_x, last_y;

  //robot.setPose(0,0,0);
  //last_x = kobuki->getPoseX();
  //last_y = kobuki->getPoseY();
  last_x = getX();
  last_y = getY();


  while (diff_dist < distance) {
    if (checkCollision() == SAFE) {
      //cout << "diff_dist=" << diff_dist << endl;
      move(0.3, 0);
      //x = kobuki->getPoseX();
      //y = kobuki->getPoseY();
      x = getX();
      y = getY();

      diff_dist = sqrt((x - last_x) * (x - last_x)
		       + (y - last_y) * (y - last_y));
    }
    else {
      //robot.stop();
      move(0, 1.0);
    }
    usleep(10*1000);
    ros::spinOnce();
  }
  move(0,0); 
}


void Robot::goAhead2Marco(double distance)
{
  double dist, last_dist, diff_dist = 0;
  double x,y, last_x, last_y;

  //robot.setPose(0,0,0);                                                                                 
  //last_x = kobuki->getPoseX();
  //last_y = kobuki->getPoseY();
  last_x = getX();
  last_y = getY();

  while (diff_dist < distance) {
    if (checkCollision() == SAFE) {
      //cout << "diff_dist=" << diff_dist << endl;                                                        
      move(0.3, 0);
      //x = kobuki->getPoseX();
      //y = kobuki->getPoseY();
      x = getX();
      y = getY();
      diff_dist = sqrt((x - last_x) * (x - last_x)
                       + (y - last_y) * (y - last_y));
    }
    else {
      //robot.stop();                                                                                     
      move(0, -1.0);
    }
    usleep(10*1000);
    ros::spinOnce();
  }
  move(0,0);
}

// by Marco
void Robot::goForwardMarco(double distance)
{
  double dist, last_dist, diff_dist = 0;
  double x,y, last_x, last_y;

  //robot.setPose(0,0,0);                                                                                 
  //last_x = kobuki->getPoseX();
  //last_y = kobuki->getPoseY();
  last_x = getX();
  last_y = getY();

  while (diff_dist < distance) {
    if (checkCollision() == SAFE) {
      //cout << "diff_dist=" << diff_dist << endl;                                              
      move(0.3, 0);
      //x = kobuki->getPoseX();
      //y = kobuki->getPoseY();
      x = getX();
      y = getY();

      diff_dist = sqrt((x - last_x) * (x - last_x)
		       + (y - last_y) * (y - last_y));
  }
  else {
    stop();
  }
  usleep(10*1000);
  ros::spinOnce();
 }
 move(0,0);
}



void Robot::goOut(double distance)
{
  double dist, last_dist, diff_dist = 0;
  double x,y, last_x, last_y;

  //last_x = kobuki->getPoseX();
  //last_y = kobuki->getPoseY();
  last_x = getX();
  last_y = getY();

  while (diff_dist < distance) {
    if (checkCollision() == SAFE) {
      move(0.3, 0.05);
      //x = kobuki->getPoseX();
      //y = kobuki->getPoseY();
      x = getX();
      y = getY();

      diff_dist = sqrt((x - last_x) * (x - last_x)
                       + (y - last_y) * (y - last_y));
    }
    else {
      move(0, 1.0);
    }
    usleep(10*1000);
    ros::spinOnce();
  }
  move(0,0);
}


void Robot::turn(double angle)
{
  double rad, last_rad, diff_rad = 0, sum  = 0;
  double tspeed = 1.0; // turn speed

  //last_rad = kobuki->getPoseTh();
  last_rad = getTheta();

  //rad = last_rad;
  static int loop = 0;
  //cout << "diff_rad=" << rad-last_rad << endl;

  while (fabs(sum) < fabs(angle) ) {
    //rad = kobuki->getPoseTh();
    rad = getTheta();
    //rad = theta;
    diff_rad = (rad - last_rad);
    if (diff_rad >=  M_PI) diff_rad =  2.0 * M_PI - diff_rad;
    if (diff_rad <= -M_PI) diff_rad = -2.0 * M_PI - diff_rad;
    sum += diff_rad;
    last_rad = rad;

    cout << "loop=" << loop++ << " rad=" << rad << endl;
    cout << "sum=" << sum << " angle=" << angle << endl;

    move(0, tspeed);
    usleep(10*1000);
    ros::spinOnce();
  } 
  //usleep(10*1000);
  move(0,0);
  usleep(100*1000);
}



void Robot::actionInElevator()
{
  // エレベータに入ったら半回転してドアが開くのを待つ
  cout << "IN Elevator" << endl;
  stop();
  if (system(SPEAK_ELEVATOR)== -1) printf("system error\n");
  cout << " I stop" << endl;
  sleep(2);

#ifdef SPEECH
  const string str = "Say, leave the elevator";
  //recognizeSimpleVoice(str);
  recognizeVoice(str);
#endif

  while (checkObstacles(2.0) == true) {
    move(0, 1.2);
    usleep(10*1000);
    ros::spinOnce();
  }

  stop();
  goOut(1.7); // 1.7m 前進
  turn(0.4 * M_PI);
  goAhead(0.6);
  stop();
  turn(1.0*M_PI);
  stop();
  sleep(6);

  ros:: spinOnce();

  // initailize for state1
  linear_speed  = 0;
  angular_speed = 0;
  last_x        = 0;
  last_y        = 0;
  last_theta    = 0;
  last_time     = 0;
  x             = 0;
  y             = 0;
  theta         = 0;
  time          = 0;
  human_lost     = true;
  human.distance = 0;
  human.angle    = 0;
  human.last_distance = 0;
  human.last_angle    = 0;
  
  second_section = true; // pass the second section
  //  state = STATE1;     //　follow the operator
}

void Robot::searchOperator()
{
  sc->say("I entered Third section");
  //sleep(3);

  static bool obstacle_flag = false;
  static bool turn_flag = false;
  double turn_speed  = 0.5;
  double init_direction = getTheta();
  // 障害物に沿って回りこむ
  
  //robot.setLinearSpeed(0.5);
  while (1) {
    ros::spinOnce();

    double angle_diff = fabs(getTheta()-init_direction);
    if (angle_diff >  2 * M_PI) angle_diff -= 2.0 * M_PI;
    if (angle_diff < -2 * M_PI) angle_diff += 2.0 * M_PI;

    if (angle_diff > 0.25 * M_PI) turn_flag = true;

    if (turn_flag == true) {
      cout << "turn flag== true" << endl;
      if (angle_diff < DEG2RAD(5)) {
	stop();
	//sc->say("Please run on the spot");
	if (system(SPEAK_WALK_IN_PLACE)== -1) printf("system error\n");
	sleep(1);
	//state = STATE1;
	return;
      }
    }

    int collision = checkCollision();
    if (collision == 0) {
      cout << "collsion == 0" << endl;
      if (obstacle_flag == false) move(0.5,  0.0);
      else                        move(0.5, -2.2);
    }
    else if (collision == DANGER) {
      cout << "collision == DANGER" << endl;
      //robot.move(0, 0.5); 
      move(0,turn_speed);
      obstacle_flag = true;
    }
    else {
      double kp = 2.0;
      move(0.5, turn_speed);
    }

    usleep(10*1000);
  }
}


double Robot::checkFrontDistanceMarco()
{
  int center = dataCount/2;
  return laser_distance[center];
}

void Robot::searchOperatorMarco()
{
  //Determine Obstacle

  if(choosePathMarco() == true) {	

  /* Turn Left */
	turn(0.4 * M_PI);
	sleep(2);
	double wallDist = checkFrontDistanceMarco() - 0.3;
	goForwardMarco(wallDist);
	sleep(2);
	turn(-0.4 * M_PI);
	sleep(2);
	goAhead2Marco(2.0);

  }
  else {
  /* Turn Right */
	turn(-0.4 * M_PI);
	sleep(2);
	double wallDist = checkFrontDistanceMarco() - 0.3;
	goForwardMarco(wallDist);
	sleep(2);
	turn(0.4 * M_PI);
	sleep(2);
	goAhead(2.0);
  }

  //Search and follow operator
  ros:: spinOnce();

  linear_speed  = 0;
  angular_speed = 0;
  last_x        = 0;
  last_y        = 0;
  last_theta    = 0;
  last_time     = 0;
  x             = 0;
  y             = 0;
  theta         = 0;
  time          = 0;
  human_lost    = true;
  human.distance = 0;
  human.angle    = 0;
  human.last_distance = 0;
  human.last_angle    = 0;
  
  stop();
  if (system(SPEAK_WALK_IN_PLACE)==-1) printf("system error\n");
  sleep(5);
  if (system(SPEAK_FIND_OPERATOR)== -1) printf("system error\n");
  sleep(5);
  
  //state = STATE1;
}


double Robot::findDirection(double distance)
{
  int center = dataCount/2;
  int search_lines = 1080 *  (180.0/270.0); // 走査線数                                                  \
                                                                                                          
  double angle,danger_radius = 0.25;  // [m]                                                             \
                                                                                                          

  int j = 0;
  for (int i = center - search_lines/2; i <= center + search_lines/2; i++) {
    angle = j * 1.5 * M_PI/1080;  // 270度で1080本                                                       \
                                                                                                          
    j++;

    double x = laser_distance[i] * cos(angle);
    double y = laser_distance[i] * sin(angle);

    // distance以内に障害物あり                                                                          \
                                                                                                          
    if ((fabs(x) <= danger_radius) && (fabs(y) <= distance)) {
      return 999;
    }
  }
  return angle;
}


bool Robot::findHuman(cv::Mat input_image, double *dist, double *angle)
{
  float leg_radius = 0.05 * mToPixel;
  Object obj[100], obj1[100], obj2[100];
  int obj_num = 0, obj1_num = 0, obj2_num = 0;

  obj_num = findLegs(input_image, obj, detect_image, lidar_image, "Cirlce 1", red,
                     10,30, 5, 21, 0.2, 1.5,  40, 160, 3400, 25000, 1.5, 0.5);

  // 人間の位置と方向を計算(ローカル座標系）
  calcHumanPose(obj_num,obj, &human);
  *dist  = human.distance;
  *angle = human.angle;

  if (human.distance == 999) return false;
  else                       return true;
}


void Robot::followPerson(cv::Mat input_image)
{
  float leg_radius = 0.05 * mToPixel;
  Object obj[100], obj1[100], obj2[100];
  int obj_num = 0, obj1_num = 0, obj2_num = 0;
  // 脚を見つけるために原画像で連結領域を探す
  // 調整するパラメータ１番目と２番目。脚断面の半径のピクセル数(1: 最小、２：最大)

  // 輪郭長：最小、最大[pix]；　外接矩形：横幅最小、最大 [pix]
  // 矩形縦横比率：最小、最大；　輪郭面積：最小、最大 [pix]
  //obj_num = findLegs(e1_img, obj, detect_image, lidar_image, "Cirlce 1", red,
  //		       10,30,5,21,0.2,1.5,40,160,3400, 25000, 1.5, 0.5);
  // 脚候補
  /* contour size: min=8 max=40 ave=14.93 sigma=0.122 
     intensity: min=90 max=159 ave=116.89 sigma=0.583 
     rect width: min=5 max=20 ave=10.87 sigma=0.053 
     ratio: min=0 max=10 ave=0.81 sigma=0.006 
     m00: min=10 max=300 ave=53.13 sigma=0.494 
     m01: min=1946 max=35882 ave=9368.25 sigma=147.445 
     m10: min=0 max=5030619 ave=13564.22 sigma=124.992 
     m11: min=496230 max=0 ave=2387218.94 sigma=37271.895 
     diff x: min=-2 max=2 ave=0.12 sigma=0.010 
     diff y: min=-2 max=1 ave=-0.57 sigma=0.010 */
  // noise
  /* contour size: min=8 max=37 ave=9.26 sigma=0.114 
     intensity: min=91 max=158 ave=113.73 sigma=0.562 
     rect width: min=5 max=33 ave=5.83 sigma=0.095 
     ratio: min=0 max=10 ave=11.06 sigma=0.009 
     m00: min=10 max=300 ave=20.48 sigma=0.557 
     m01: min=714 max=20316 ave=2157.56 sigma=62.957 
     m10: min=0 max=410172 ave=5104.15 sigma=140.407 
     m11: min=171360 max=0 ave=538160.76 sigma=15938.023 
     diff x: min=-0.50 max=1.03 ave=0.00 sigma=0.002 
     diff y: min=-4.69 max=0.28 ave=-0.05 sigma=0.013  */
  
  /* contour size: min=8 max=46 ave=16.39 sigma=0.374 
intensity: min=40 max=190 ave=163.55 sigma=0.391 
rect width: min=5 max=23 ave=10.73 sigma=0.242 
ratio: min=0 max=10 ave=0.85 sigma=0.007 
m00: min=5 max=1000 ave=55.29 sigma=1.746 
m01: min=2814 max=33744 ave=11266.02 sigma=361.226 
m10: min=5 max=729624 ave=13896.16 sigma=436.956 
m11: min=652848 max=0 ave=2834521.39 sigma=90816.620 
diff x: min=-0.96 max=1.57 ave=0.17 sigma=0.012 
diff y: min=-1.66 max=1.97 ave=0.08 sigma=0.018 */

#ifdef MOMENT_EXEL
  //  obj_num = findLegs(input_image, obj, detect_image, lidar_image, "Cirlce 1", red,
  //		     5,60, 4, 40, 0, 10, 10, 300, 0, 2500000, 1.5, 0.3);
 obj_num = findLegs(input_image, obj, detect_image, lidar_image, "Cirlce 1", red,
		     0,1000, 0, 1000, 0, 10, 5, 1000, 5, 2500000, 1.5, 2.0);
#else
  // 最小最大を入れる
  // FMTでうまくいっているしきい値
  //obj_num = findLegs(input_image, obj, detect_image, lidar_image, "Cirlce 1", red,
  //		     10,30, 5, 21, 0.2, 1.5, 40, 160, 3400, 25000, 1.5, 0.5);
  // 家でのしきい値
  obj_num = findLegs(input_image, obj, detect_image, lidar_image, "Cirlce 1", red,
 		     10,30, 5, 21, 0.2, 1.5,  40, 160, 3400, 25000, 1.5, 0.5);
#endif
  
  // FMTでうまく行ったデータ
  //obj_num = findLegs(e1_img, obj, lidar_image, "Cirlce 1", red,
  //                   10,30,5,21,0.2,1.5,40,160,3400, 25000, 1.5, 0.5);
  
  // 人間の位置と方向を計算(ローカル座標系）
  calcHumanPose(obj_num,obj, &human);

  //cv::circle(lidar_image,cv::Point(human.image_pos.x, human.image_pos.y),5,green,2); 


  /*cout << "robot x=" << robot.getX() << " y=" << robot.getY()
       << " theta=" << robot.getTheta() <<endl;

  if (human.getX() !=999) {
    cout << "human x=" << human.getX() << " y=" << human.getY() 
    	 << " theta=" << human.getTheta() <<endl;
    cout << "human local x=" << human.local.x << " y=" << human.local.y
	 << " theta=" << human.local.theta <<endl << endl;
	 } */

  //human.distance = tmp_distance;
  //human.angle    = tmp_angle;
  //printf("human.distance=%.1f angle=%.1f \n",human.distance,RAD2DEG(human.angle));

  //#ifdef DEBUG
  //cout << "human dist=" << human.distance << " last dist=" << human.last_distance << " angle=" << human.angle << endl;
  //#endif 

  cv::waitKey(1);
  
  // 安定して検出できないための工夫
  //static bool LOST = false;
  // 単純移動平均フィルタ
  const int length = 3;
  static double dist_data[length], angle_data[length];
  static double posx_data[length], posy_data[length];
  static double local_vel_data[length];

  /* human.distance    = SMAfilter(human.distance, dist_data,  length);
  human.angle       = SMAfilter(human.angle,    angle_data, length);
  human.image_pos.x = SMAfilter(human.image_pos.x, posx_data, length);
  human.image_pos.y = SMAfilter(human.image_pos.y, posy_data, length);
  human.local.velocity = SMAfilter(human.local.velocity, local_vel_data, length);
  cout << "x=" <<  human.image_pos.x << " y=" << human.image_pos.y << endl; */

  static double time1;

  static int counter = 0;
  int interval = 10;
  
  if (counter++ % interval == 0) {
    double dt = (double) (cv::getTickCount()-time1)/(cv::getTickFrequency());
      
    if (dt == 0 || human.last_distance == 999 || human.distance == 999){
      human.local.velocity = 999;
    }
    else {
      human.local.velocity = (human.distance - human.last_distance)/dt;
    }
    //printf("human velocity=%6.2f distance=%f last=%f dt=%9.5f \n", human.local.velocity, human.distance, human.last_distance,dt);

    //human.last_distance = human.distance;
    time1 = cv::getTickCount();
  }

  // show detected human
  cv::circle(lidar_image,cv::Point(human.image_pos.x, human.image_pos.y),5,green,2);


  static int lost_count = 0;
  const int lost_count_max = 1; // この回数だけ連続して失敗するとロスト
  if (human.distance == 999) lost_count++;
  else {
    human_lost = false;
    lost_count = 0;
    //human.last_distance = human.distance;
    //human.last_angle    = human.angle;
  }
  // 連続lost_count_max回発見できなかったらロスト
  if (lost_count >= lost_count_max) {
    human_lost = true;
    // sc->say("I lost you");
    //sc->say("Please walk slowly");
    //sleepok(2, nh); 
  }
  if (human_lost ) { // ロストしたときは１時刻前の値を使う
    human.distance = human.last_distance;
    human.angle    = human.last_angle;

    /* if (second_section == true) {
      robot.stop();
      state = STATE3;
      return;
      } */
  } 

  //std::cout << "loop:"<< loop++ <<std::endl;
  // 999 はエラーなので速度を0にセット
  if (human.distance == 999) setLinearSpeed(0);
  // 人が追跡距離内にいる場合
  else if (((human.distance >=  follow_min_distance) &&
	    (human.distance <=  follow_max_distance))) {
    double diff = follow_distance - human.distance;
    
    double tmp_speed = 0;
    if (fabs(diff) > 0.1)  	{
      double tmp_speed = getLinearSpeed();
      if (tmp_speed !=999)
	tmp_speed += gain_linear * diff;  // 近づきすぎ

      if (human.distance >= 0.5) {
	if (tmp_speed > linear_max_speed) tmp_speed = linear_max_speed;
	if (tmp_speed < 0)                tmp_speed = 0;
      }
      else {
	if (tmp_speed > 0.2)              tmp_speed = 0.2;
	if (tmp_speed < 0)                tmp_speed = 0;
      }
      setLinearSpeed(tmp_speed);
    }
      
    //if (human.distance < follow_min_distance)
    //	robot.setLinearSpeed(0);
     
    //double diff = follow_distance - human.distance;
    //cmd_speed.linear.x = gain * diff;
      
  


    // if (robot.getLinearSpeed() >   linear_max_speed)
    //  robot.setLinearSpeed(linear_max_speed);
    //if (robot.getLinearSpeed() < 0)
    //  robot.setLinearSpeed(0);
  }
  // 人が追跡距離外に出た場合は停止する
  else {
    setLinearSpeed(0);
    setAngularSpeed(0);
 }
  
  // 5度以内のときは回転しない 
  int64 old;
  double dt = (double) (cv::getTickCount()-old)/(cv::getTickFrequency());
  if ((human.angle == 999) || (human.last_angle == 999)) {
      setAngularSpeed(0);
  }
  else if (fabs(human.angle) > DEG2RAD(5.0)) {
    double tmp_speed = getAngularSpeed();
    tmp_speed = tmp_speed - kp * human.angle - kd * (human.angle - human.last_angle);
    setAngularSpeed(tmp_speed);
  }
  human.last_angle = human.angle;
  old = cv::getTickCount();

  // 速度の上限と下限を設定
  if (getAngularSpeed() >  turn_max_speed) 
    setAngularSpeed(turn_max_speed);
  if (getAngularSpeed() < -turn_max_speed) 
    setAngularSpeed(-turn_max_speed);
  
  // 比例航法
#ifdef PROPORTIONAL_NAVI
  // 角加速度情報も利用 PD制御
  static doublle last_omega=0, last_alpha = 0;;
  double omega, alpha;
  // 5度以内のときは回転しない                                                         
  // 極座標系に基づく方法
  human.velocity = 1.0; // 本来入れるべきだがvelocityが安定しないため挙動がふらつく

  double dt = (double) (cv::getTickCount()-old)/(cv::getTickFrequency());

  if (fabs(human.angle) > DEG2RAD(0.0)) {
    if ((human.angle != 999) && (human.last_angle != 999) && (human.velocity != 999) {
	if (dt != 0) {
	  omega = (human.angle - human.last_angle)/dt;
	  alpha = (Uomega - last_omega)/dt;
	}
	else {
	  omega = 0;
	  alpha = 0;
	}
    }
    else {
      omega = 0;
      alpha = 0;
    }
      double speed = - kp * human.velocity * omega - kd * human.velocity * alpha;
      setAngularSpeed(speed);
      }
      
    old = cv::getTickCount();      
    last_omega = omega;

  // 直交座標系に基づく方法
  // 比例航法は金沢工業高等専門学校伊藤恒平先生の文献に基づく          
  // 伊藤恒平, 村田慎太郎, 山田高徳,"DGPS と比例航法を用いた移動ロボットの誘導に関する一考察"
  // 論文集「高専教育」 第32 号 2009.3  

  /* Vector3d robotPos;
  static double old_x = human.local.x;
  static double old_y = human.local.y;

  double dt = (double) (cv::getTickCount()-old)/(cv::getTickFrequency());
  //LOG_MSG(("dt=%f\n",dt));                                                    
          
  if (dt != 0) {
    dx     = (human.local.x - old_x)/dt;
    dy     = (human.local.y - old_y)/dt;
  }
  else {
    dx = 0;
    dy = 0;
  }

  old = cv::getTickCount();
  old_x = human.local.x;
  old_y = human.local.y;

  double omega;
  if ((diff_x*diff_x + diff_y*diff_y) != 0) {
    omega = (- dy * diff_x + dx * diff_y)/(diff_x*diff_x + diff_y*diff_y);
  }
  else {
    omega = 0;
  }

  double nav_param = 3; // 航法定数  3                                                     
  // 移動開始　角速度(left, right)                                                        
  robot.setAngularSpeed(- gain_proportion * omega); */



  //robot.cmd_speed.angular.z = - gain_proportion * angle_change;
  // cout << "angle_change" << angle_change << "last_angle=" << human.last_angle << " angle=" << human.angle << endl;
  //cout << "angular.z=" << robot.getAngularSpeed() << endl;
#endif
  //printf("angle=%.1f speed.z=%.1f distance=%.2f speed.x=%.1f\n",
  //	   RAD2DEG(human.angle), cmd_speed.angular.z, human.distance, cmd_speed.linear.x);
  
  
#ifdef MOVE
  // 衝突検出

  int collision = checkCollision();
  //cout << "Collision=" << collision << endl;
  if (collision == 0) {
    move(getLinearSpeed(),getAngularSpeed());
  }
  // エレベータの中での動作
  else if (collision == IN_ELEVATOR) {
    //state = STATE2;
    return;
  }
  // 衝突する可能性が極めて高いのでゆっくりその場回転 
  else if (collision == DANGER) {
    //robot.move(0, 0.5); 
    move(0,0);
  }
  // 衝突しないように向きだけ変更    
  else { 
    double kp = 2.0;
    move(getLinearSpeed(),DEG2RAD(collision * kp));
  }
#endif
  
  //if (human.angle != 999) {  // 人を見つけている時に現在の値を次の変数に保存
    human.last_distance = human.distance;
    human.last_angle    = human.angle;
    //}

}

void Robot::prepWindow()
{
  cv::Mat d3_img, e2_img, tmp_img,final_img;
  cv::Mat e1_bin_img, e2_bin_img, e3_bin_img;
  cv::Mat e2d1_img,e2_color_img;
  cv::Mat lidar_bin_image, rec_img;
  cv::Mat bin_img1, bin_img2, bin_img3;
  
  // グレースケールに変換する
  // gray scale -> binary                                                           
  cv::threshold(lidar_gray_image, lidar_bin_image, 0, 255,
		cv::THRESH_BINARY| cv::THRESH_OTSU);
  
  //cv::namedWindow( "Lidar bin image", CV_WINDOW_AUTOSIZE);
  lidar_bin_image = ~lidar_bin_image;
  //cv::imshow("Lidar bin image",lidar_bin_image);
  cv::erode(lidar_bin_image, e1_img, cv::Mat(), cv::Point(-1,-1), 1);
  cv::erode(lidar_bin_image, e2_img, cv::Mat(), cv::Point(-1,-1), 2);
  cv::erode(lidar_bin_image, e3_img, cv::Mat(), cv::Point(-1,-1), 3);
  //cv::erode(lidar_bin_image, e4_img, cv::Mat(), cv::Point(-1,-1), 4);
  cv::threshold(e2_img, e2_bin_img, 0, 255,
		cv::THRESH_BINARY| cv::THRESH_OTSU);
  cv::dilate(e2_img, e2d1_img, cv::Mat(), cv::Point(-1,-1), 1);
  
  cv::Mat lidar_color_image;
  cv::cvtColor(lidar_gray_image, lidar_color_image, CV_GRAY2BGR);
  lidar_image = lidar_color_image;
}

void Robot::showWindow()
{
  cv::namedWindow( "Map", CV_WINDOW_AUTOSIZE );
  cv::Mat dst_img1 = ~lidar_image;
  cv::imshow("Map",dst_img1);
  
  //cv::namedWindow( "Detected Legs", CV_WINDOW_AUTOSIZE );
  //cv::Mat dst_img2 = ~lidar_gray_image;
  //cv::imshow("Detected Legs",dst_img2);
  ////cv::imshow("Detected Legs",lidar_image);

  cv::Mat e1_bin_img;
  cv::threshold(e1_img, e1_bin_img, 0, 255,
		cv::THRESH_BINARY| cv::THRESH_OTSU);
  //cv::namedWindow( "Lidar bin image", CV_WINDOW_AUTOSIZE );
  //cv::imshow("Lidar bin image",~e1_bin_img);
  
  //cv::Mat diff_img;
  //diff_img = e1_old_img.clone();
  //cv::absdiff(e1_img, e1_old_img, diff_img);
  //cv::bitwise_xor(e1_img, e1_old_img, diff_img);
  //cv::namedWindow( "Diff", CV_WINDOW_AUTOSIZE );
  diff_img = e1_img ^ e1_old_img;
  cv::Mat d1_img;
  cv::erode(diff_img, d1_img, cv::Mat(), cv::Point(-1,-1), 1);               
  //cv::imshow("Diff",~diff_img);
  //cv::imshow("Diff",~d1_img);
  e1_old_img = e1_img.clone();
  
  
  //cv::namedWindow( "diff3 img", CV_WINDOW_AUTOSIZE );
  diff3_img = e3_img ^ e3_old_img;
  cv::erode(diff3_img, d1_img, cv::Mat(), cv::Point(-1,-1), 1);
  //cv::imshow("diff3 img",~d1_img);
  e3_old_img = e3_img.clone();
  
  // 動画を取るときは以下をコメントアウトする
  // writer << 取り込みたイメージ名
#ifdef RECORD
  record(writer1, ~lidar_image);
  //record(writer2, ~lidar_gray_image);
  //writer1 << ~lidar_image;
  //writer << white_image2;
  //writer2 << e4_img;
#endif
}

void Robot::speechCallback(const std_msgs::String& voice)
{

  ::voice_command = voice;
  //cout << "speechCB:" << voice << endl;

  if (::voice_command.data == "mini"){
    recog_mini = true;
    sc->startWave(chime);
    sleep(3);
  }
  else {
    if (recog_mini == true) {
      if (::voice_command.data == "stop"){
	sc->say("You said stop");
	sleep(3);
	recog_stop = true;
	recog_mini = false;
      }
      else if (::voice_command.data == "follow me"){
	sc->say("follow me");
	sleep(3);
        recog_followme = true;
        recog_mini = false;
      }
      else if (::voice_command.data == "leave the elevator"){
	sc->say("leave the elevator");
	sleep(3);
        recog_leave = true;
        recog_mini = false;
      }
    }
    //usleep(10*1000);
  }
}

// Callback function:  Get the present robot position   
void  Robot::amclPoseCallback(const geometry_msgs::PoseWithCovarianceStamped::ConstPtr& msg)
 {
   robotPose.header = msg->header;
   robotPose.pose   = msg->pose.pose;
   //ROS_INFO("robotPose.pose.position.x = %.2f y=%.2f",
   //	    robotPose.pose.position.x,robotPose.pose.position.y);
 }



double wrap_angle(const double &angle)
{
  double wrapped;
  if ((angle <= M_PI) && (angle >= -M_PI)) {
    wrapped = angle;
  }
  else if (angle < 0.0) {
    wrapped = fmodf(angle - M_PI, 2.0 * M_PI) + M_PI;
  }
  else {
    wrapped = fmodf(angle + M_PI, 2.0 * M_PI) - M_PI;
  }
  return wrapped;
}

void Robot::odomCallback(const nav_msgs::OdometryConstPtr& msg)
{
  x = msg->pose.pose.position.x;
  y = msg->pose.pose.position.y;
  theta = tf::getYaw(msg->pose.pose.orientation);
  theta = wrap_angle(theta);

  //cout << "theta=" << theta << endl;

  vx  = msg->twist.twist.linear.x;
  vy  = msg->twist.twist.linear.y;
  vth = msg->twist.twist.angular.z;
}

int Robot::navigation(MoveBaseClient *ac)
//int Robot::navigation()
{
  //tell the action client that we want to spin a thread by default
  //MoveBaseClient ac("move_base", true);

  ros::Rate rate(30); // 10 Hz
  int num = getWaypointNum();

  //while (waypoint[num][2] !=777) {
  //wait for the action server to come up
  while(!ac->waitForServer(ros::Duration(5.0))){
    ROS_INFO("Waiting for the move_base action server to come up");
  }

  ros::spinOnce();

  goal.target_pose.pose.position.x = waypoint[num][0];
  goal.target_pose.pose.position.y = waypoint[num][1];
  double target_dir = waypoint[num][2];
  //double radians    = target_dir * (M_PI/180);                                                       
  double radians  = target_dir;
  tf::Quaternion quaternion;
  quaternion = tf::createQuaternionFromYaw(radians);

  geometry_msgs::Quaternion qMsg;
  tf::quaternionTFToMsg(quaternion, qMsg);
  goal.target_pose.pose.orientation = qMsg;

  ROS_INFO("Sending the next waypoint No.%d",num);
  ac->sendGoal(goal);

  bool result = false;
  // 下の関数は結果がでるまでブロックされる 
  result = ac->waitForResult(ros::Duration(300.0));

    
  /* while (!result) {
     ros::spinOnce();
      double x = robotPose.pose.position.x;
      double y = robotPose.pose.position.y;
      //ROS_INFO("WP(%.1f,%.1f) Pos(%.1f,%.1f)\n",
      //       waypoint[num][0],waypoint[num][1],x,y);

      double diff_x = robotPose.pose.position.x - goal.target_pose.pose.position.x;
      double diff_y = robotPose.pose.position.y - goal.target_pose.pose.position.y;
      double diff_yaw = tf::getYaw(robotPose.pose.orientation) - target_dir;
      double dist = sqrt(diff_x*diff_x + diff_y*diff_y);
      diff_yaw = fabs(diff_yaw);
      //ROS_INFO("Diff:Dist(%.1f) Dir(%.4f) WP(%.1f,%.1f) Pos(%.1f,%.1f)\n",dist,
      //         diff_yaw, waypoint[num][0],waypoint[num][1],x,y);

      bool flag = false;
      double dist_goal_threshold = 0.25; // この距離内に入ったらウェイポイントに到達したとみなす
      double dir_goal_threshold = 32*M_PI/180.0; // 32 角度

      if ((dist <= dist_goal_threshold)
	  && (diff_yaw <= dir_goal_threshold)) { // M_PI/180                                        
	result = true;
	} */



      //　ロボットがウェイポイントに近づき、その方向を向いた場合
      /* if ((num == 20) && (dist_goal_threshold * 3.0 < dist) 
	  && (dist < dist_goal_threshold * 6.0) && (diff_yaw < dir_goal_threshold)) {
	// ウェイポイント２の周りには障害物がある
	double obj_dist = measureObstacleDist(15*M_PI/180.0);
	ROS_INFO("obj_dist=%.1f dist=%.1f\n",obj_dist, dist);
	if (obj_dist < dist) {
	  // 障害物が前方にある場合
	  for (int i=0; i < 10; i++) {
	    prepWindow();
	    //ros::spinOnce();
	    double dist, angle;
	    flag = findHuman(e1_img, &dist, &angle);
	    if ( (dist > 1.5)) {
	      flag = false;
	    }
	    else {
	      break;
	    }
	    showWindow();
	    usleep(10*1000);
	  }
	}
	if (flag == true) {
	  if (system(SPEAK_FIND)==-1) printf("system error\n");
	  usleep(1000*1000);
	  return MOVE_PERSON_STATE;
	}
	else {
	  //sc->say("I find object");
	  //sleep(2);
	  //goAhead(dist);
	  //return MOVE_OBJECT_STATE;
	}
      }
      else {
	if ((dist <= dist_goal_threshold)
	    && (diff_yaw <= dir_goal_threshold)) { // M_PI/180
	  result = true;
	}
	} */
      /* usleep(5*1000);
    }
      */

  ros::spinOnce();
  if (result) {
    actionlib::SimpleClientGoalState state = ac->getState();

    
    ROS_INFO("Action finished: %s",state.toString().c_str());
    ROS_INFO("Arrived at waypoint No.%d",num);

    //stringstream s_num;
      //s_num << num;
      //string msg;
      //msg = "I reached the waypoint" + s_num.str();
      //sc->say(msg);
      //usleep(2000*1000);
    switch (num) {
    case 1:
      if (system(SPEAK_WAYPOINT1)==-1) printf("system error\n"); break;
    case 2:
      if (system(SPEAK_WAYPOINT2)==-1) printf("system error\n"); break;
    case 3:
      if (system(SPEAK_WAYPOINT3)==-1) printf("system error\n"); break;
    case 4:
      if (system(SPEAK_WAYPOINT4)==-1) printf("system error\n"); break;
    case 5:
      if (system(SPEAK_WAYPOINT5)==-1) printf("system error\n"); break;
    }
    usleep(2000*1000);
  }
  else {
    ROS_INFO("Action did not finish before the time out. ");
    sc->say("Action time out");
    sleep(2);
  }

  ros::spinOnce();
  rate.sleep();

  switch (num) {
  case 1: return WP1_STATE; 
  case 2: return WP2_STATE; 
  case 3: 
    if (getReachedWP4() == false) return WP3_STATE;
    else                          return WP3_AGAIN_STATE;
  case 5: return GOAL_STATE;
  default: 
    sc->say("Error waypoint number");
    sleep(3);
  }
  //}
}

 
void Robot::init()
{
  //system(SPEAK_START); 

  // Preparation of video recording
  //#ifdef RECORD
  //robot.prepRecord();
  //#endif


  // Set the subscribers
  speechSubscriber = nh.subscribe("/recognizer/output",5,&Robot::speechCallback,this);
  laserSubscriber  = nh.subscribe("/scan", 100, &Robot::laserCallback,this);
  odomSubscriber   = nh.subscribe("/odom", 100, &Robot::odomCallback,this);
  amclPoseSubscriber = nh.subscribe<geometry_msgs::PoseWithCovarianceStamped>("amcl_pose",
			 1, &Robot::amclPoseCallback,this);

  // Set the publisher
  cmdVelPublisher = nh.advertise<geometry_msgs::Twist>("/cmd_vel_mux/input/teleop", 100);

  // Rest the sound client
  sc.reset(new sound_play::SoundClient());

  //we'll send a goal to the robot
  //goal.target_pose.header.frame_id = "base_link";
  // サンプルプログラムでbase_linkになっていたせいで、
  // ウェイポイントナビゲーションにハマってしまった。 
  goal.target_pose.header.frame_id = "map";
  goal.target_pose.header.stamp = ros::Time::now();

}


// 状態WP_STATEの関数  
void Robot::findAction()
{
  int count = 0;
  double dist, angle;
  while (!findHuman(e1_img,&dist, &angle)) {
    ros::spinOnce();
    usleep(5*1000);
    cout << "count3=" << count++ << endl;
  }
  //sc->say("I find a human");
  if (system(SPEAK_FIND)==-1) printf("system error\n");
  usleep(2000*1000);
}

// 状態FOLLOW_STATEの関数
void Robot::followAction()
{
  //sc->say("I will follow you");                                                                    
  if (system(SPEAK_FOLLOWYOU)==-1) printf("system error\n");
  usleep(2000*1000);
  int count = 0;
  ros::spinOnce();
  memorizeOperator();
  
  while (1) {
    prepWindow();
    followPerson(e1_img);
    showWindow();
    usleep(5*1000);
    ros::spinOnce();
    //cout << "count=" << count++ << endl;
    //recognizeVoice("stop");
    if (recog_stop == true) {
      setState(WP4_STATE);
      return;
    }
  }
}

void Robot::speak(const string str, int wait_time)
{
   sc->say(str);
   usleep(wait_time*1000);
}

// main関数
int main(int argc, char* argv[])
{
  ros::init(argc, argv, "dfollow"); // ROS initilization

  // Rest the sound client  
  sc.reset(new sound_play::SoundClient());

  //tell the action client that we want to spin a thread by default
  MoveBaseClient ac("move_base", true);


  Robot robot;
  robot.welcomeMessage(); // Welcome Message
  robot.init();           // robot init

  // Preparation of video recording
  #ifdef RECORD
  robot.prepRecord();
  #endif

  //tell the action client that we want to spin a thread by default
  //MoveBaseClient ac("move_base", true); 

  ros::Rate loop_rate(33); // 33Hz  

  //move_base_msgs::MoveBaseGoal goal;

  //we'll send a goal to the robot                                                                 
  //goal.target_pose.header.frame_id = "base_link";                                                
  // サンプルプログラムでbase_linkになっていたせいで、                                             
  // ウェイポイントナビゲーションにハマってしまった。                                              
  //goal.target_pose.header.frame_id = "map";
  //goal.target_pose.header.stamp = ros::Time::now();
  

  int loop = 0;
  robot.setState(START_STATE);
  //robot.setState(WP1_STATE);
                                                                           
  while(ros::ok()) {
    // Measure time begin
    int64 time = cv::getTickCount();
    robot.prepWindow();
    if (loop++ < 10) {
      loop_rate.sleep();
      ros::spinOnce();
      continue;
    }

    switch (robot.getState()) {
    case START_STATE:
      printf("check door open \n");
      while (1) {
	if (system(SPEAK_SAY_OPEN)==-1) printf("system error\n");
	sleep(3);
	robot.prepWindow();
	if (robot.checkDoorOpen()) {
	  break;
	}
	robot.showWindow();
	ros::spinOnce();
	usleep(1000*1000);
      }

      robot.setWaypointNum(1);
      ROS_INFO("Start");
      //sc->say("My name is mini. Start");
      if (system(SPEAK_MINI)==-1) printf("system error\n");
      usleep(500*1000);
      if (system(SPEAK_COUNTDOWN)==-1) printf("system error\n");
      usleep(2000*1000);
      //system(SPEAK_START_MINI);
      //usleep(2500*1000);
      robot.setState(NAVI_STATE);
      break;
    case WP1_STATE: 
      robot.setWaypointNum(2);
      if (system(SPEAK_NEXT)==-1) printf("system error\n");
      usleep(500*1000);
      ROS_INFO("Go to the next waypoint No.%d",2);
      if (system(SPEAK_WAYPOINT2)==-1) printf("system error\n");
      usleep(1000*1000);
      robot.setState(NAVI_STATE);
      break;                                             
    case WP2_STATE:  
      robot.setWaypointNum(3);
      if (system(SPEAK_NEXT)==-1) printf("system error\n");
      usleep(1000*1000);
      ROS_INFO("Go to the next waypoint No.%d",3);
      if (system(SPEAK_WAYPOINT3)==-1) printf("system error\n");
      usleep(2000*1000);
      robot.setState(NAVI_STATE);
      break;
    case WP3_STATE: {
      cout << "WP3_STATE" << endl;
      robot.setWaypointNum(4);
      robot.findAction();
      robot.setState(FOLLOW_STATE);
      break;
    }
    case WP3_AGAIN_STATE:
      printf("WP3_AGAIN_STATE\n");
      sc->say("Waypoin 3 again state");
      sleep(2);
      robot.setWaypointNum(5); // 4 is the goal point
      ROS_INFO("I leave this arena");
      if (system(SPEAK_LEAVE_ARENA)==-1) printf("system error\n");
      usleep(2000*1000);
      //robot.setWaypointNum(4);
      robot.setState(NAVI_STATE);
      break;
    case WP4_STATE:   
      printf("WP4_STATE\n");
      sc->say("Waypoint 4 state");
      sleep(2);
      robot.setWaypointNum(3);
      ROS_INFO("Go to the Waypoint3 again");
      sc->say("Next Waypoint3 again");
      sleep(2);
      robot.setReachedWP4();
      robot.setState(NAVI_STATE);
      break;
    case NAVI_STATE: { 
      int state  = robot.navigation(&ac); 
      //int state =robot.navigation();
      robot.setState(state); // 複数の状態に遷移する
      break;
    }
    case MOVE_PERSON_STATE:
      robot.stop();
      //robot.speak("Please move out", 3000);
      if (system(SPEAK_MOVEOUT)==-1) printf("system error\n");
      usleep(2000*1000);
      robot.setState(NAVI_STATE);
      break;                                             
    case MOVE_OBJECT_STATE: 
      // あとで物体を移動する関数を実装 
      //sc->say("move object state");
      //usleep(1500*1000);
      //robot.stop();
      //robot.goAhead(1.0); // 1m進む
      //robot.speak("Move out", 2000);
      robot.setState(NAVI_STATE);
      break;                                             
    case FOLLOW_STATE: {
      cout << "FOLLOW_STATE" << endl;
      robot.followAction();
      robot.setState(WP4_STATE);
      break;
    }
    case GOAL_STATE:  
      robot.stop();
      if (system(SPEAK_COMPLETED)==-1) printf("system error\n");
      usleep(2000*1000);
      break;                                             
    default: cout << "error state" << endl;         break;                                             
    }                                                                          

    robot.showWindow();
    loop_rate.sleep();
    ros::spinOnce();
  }

  ROS_INFO("Finished\n");

  return 0;
}


