/*
 *  Copyright (c) 2017, Tier IV, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of Autoware nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <iostream>
#include <thread>
#include <chrono>
#include <map>

#include <ros/ros.h>
#include <std_msgs/Bool.h>
#include <geometry_msgs/TwistStamped.h>

#include "mqtt_socket_msgs/RemoteCmd.h"
#include "mqtt_socket_msgs/SelectCmd.h"
#include "tablet_socket_msgs/mode_cmd.h"
#include "tablet_socket_msgs/gear_cmd.h"
#include "runtime_manager/accel_cmd.h"
#include "runtime_manager/brake_cmd.h"
#include "runtime_manager/steer_cmd.h"
#include "waypoint_follower_msgs/ControlCommandStamped.h"

class SelectCommand
{
  using remote_msgs_t = mqtt_socket_msgs::RemoteCmd;
  using select_msgs_t = mqtt_socket_msgs::SelectCmd;

  public:
    SelectCommand(const ros::NodeHandle& nh, const ros::NodeHandle& private_nh);
    ~SelectCommand();
  private:
    void watchdog_timer();
    void remote_cmd_callback(const remote_msgs_t::ConstPtr& input_msg);
    void auto_cmd_twist_cmd_callback(const geometry_msgs::TwistStamped::ConstPtr& input_msg);
    void auto_cmd_mode_cmd_callback(const tablet_socket_msgs::mode_cmd::ConstPtr& input_msg);
    void auto_cmd_gear_cmd_callback(const tablet_socket_msgs::gear_cmd::ConstPtr& input_msg);
    void auto_cmd_accel_cmd_callback(const runtime_manager::accel_cmd::ConstPtr& input_msg);
    void auto_cmd_steer_cmd_callback(const runtime_manager::steer_cmd::ConstPtr& input_msg);
    void auto_cmd_brake_cmd_callback(const runtime_manager::brake_cmd::ConstPtr& input_msg);
    void auto_cmd_ctrl_cmd_callback(const waypoint_follower_msgs::ControlCommandStamped::ConstPtr& input_msg);

    void reset_select_cmd_msg();

    ros::NodeHandle nh_;
    ros::NodeHandle private_nh_; 
    ros::Publisher emergency_stop_pub_;
    ros::Publisher select_cmd_pub_;
    ros::Subscriber remote_cmd_sub_;
    std::map<std::string , ros::Subscriber> auto_cmd_sub_stdmap_;

    select_msgs_t select_cmd_msg_;
    std_msgs::Bool emergency_stop_msg_;
    ros::Time remote_cmd_time_;
    ros::Duration timeout_period_;

    std::thread watchdog_timer_thread_;
    enum class CommandMode{AUTO=1, REMOTE} command_mode_;
};

SelectCommand::SelectCommand(const ros::NodeHandle& nh, const ros::NodeHandle& private_nh) :
     nh_(nh)
    ,private_nh_(private_nh)
    ,timeout_period_(1.0)
    ,command_mode_(CommandMode::REMOTE)
{
  emergency_stop_pub_ = nh_.advertise<std_msgs::Bool>("/emergency_stop", 1, true);
  select_cmd_pub_ = nh_.advertise<select_msgs_t>("/select_cmd", 1, true);

  remote_cmd_sub_ = nh_.subscribe("/remote_cmd", 1, &SelectCommand::remote_cmd_callback, this);

  auto_cmd_sub_stdmap_["twist_cmd"] = nh_.subscribe("/twist_cmd", 1, &SelectCommand::auto_cmd_twist_cmd_callback, this);
  auto_cmd_sub_stdmap_["mode_cmd"] = nh_.subscribe("/mode_cmd", 1, &SelectCommand::auto_cmd_mode_cmd_callback, this);
  auto_cmd_sub_stdmap_["gear_cmd"] = nh_.subscribe("/gear_cmd", 1, &SelectCommand::auto_cmd_gear_cmd_callback, this);
  auto_cmd_sub_stdmap_["accel_cmd"] = nh_.subscribe("/accel_cmd", 1, &SelectCommand::auto_cmd_accel_cmd_callback, this);
  auto_cmd_sub_stdmap_["steer_cmd"] = nh_.subscribe("/steer_cmd", 1, &SelectCommand::auto_cmd_steer_cmd_callback, this);
  auto_cmd_sub_stdmap_["brake_cmd"] = nh_.subscribe("/brake_cmd", 1, &SelectCommand::auto_cmd_brake_cmd_callback, this);
  auto_cmd_sub_stdmap_["ctrl_cmd"] = nh_.subscribe("/ctrl_cmd", 1, &SelectCommand::auto_cmd_ctrl_cmd_callback, this);

  select_cmd_msg_.header.seq = 0;

  emergency_stop_msg_.data = false;

  remote_cmd_time_ = ros::Time::now();
  watchdog_timer_thread_ = std::thread(&SelectCommand::watchdog_timer, this);
  watchdog_timer_thread_.detach();
}

SelectCommand::~SelectCommand()
{
}

void SelectCommand::reset_select_cmd_msg()
{
  select_cmd_msg_.linear_x        = 0;
  select_cmd_msg_.angular_z       = 0;
  select_cmd_msg_.mode            = 0;
  select_cmd_msg_.gear            = 0;
  select_cmd_msg_.accel           = 0;
  select_cmd_msg_.brake           = 0;
  select_cmd_msg_.steer           = 0;
  select_cmd_msg_.linear_velocity = -1;
  select_cmd_msg_.steering_angle  = 0;
}

void SelectCommand::watchdog_timer()
{
  while(1)
  {
    ros::Time now_time = ros::Time::now();

    if(now_time - remote_cmd_time_ >  timeout_period_ 
       || emergency_stop_msg_.data == true)
    {
        command_mode_ = CommandMode::AUTO;
        emergency_stop_msg_.data = true;
        emergency_stop_pub_.publish(emergency_stop_msg_);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::cout << "c_mode:"     << static_cast<int>(command_mode_)
              << " e_stop:"    << static_cast<bool>(emergency_stop_msg_.data)
              << " diff_time:" << (now_time - remote_cmd_time_).toSec()
              << std::endl;
  }
}

void SelectCommand::remote_cmd_callback(const remote_msgs_t::ConstPtr& input_msg)
{
  command_mode_ = static_cast<CommandMode>(input_msg->mode);
  select_cmd_msg_.mode = input_msg->mode;
  emergency_stop_msg_.data = static_cast<bool>(input_msg->emergency);
  remote_cmd_time_ = ros::Time::now();

  if(command_mode_ == CommandMode::REMOTE)
  {
    select_cmd_msg_.header.frame_id = input_msg->header.frame_id;
    select_cmd_msg_.header.stamp = input_msg->header.stamp;
    select_cmd_msg_.header.seq++;
    select_cmd_msg_.accel = input_msg->accel;
    select_cmd_msg_.brake = input_msg->brake;
    select_cmd_msg_.steer = input_msg->steer;
    select_cmd_msg_.gear = input_msg->gear;
    select_cmd_msg_.mode = input_msg->mode;
    select_cmd_msg_.emergency = input_msg->emergency;
    select_cmd_pub_.publish(select_cmd_msg_);
  }
}

void SelectCommand::auto_cmd_twist_cmd_callback(const geometry_msgs::TwistStamped::ConstPtr& input_msg)
{
  if(command_mode_ == CommandMode::AUTO)
  {
    select_cmd_msg_.header.frame_id = input_msg->header.frame_id;
    select_cmd_msg_.header.stamp = input_msg->header.stamp;
    select_cmd_msg_.header.seq++;
    select_cmd_msg_.linear_x = input_msg->twist.linear.x;
    select_cmd_msg_.angular_z = input_msg->twist.angular.z;
    select_cmd_pub_.publish(select_cmd_msg_);
  }
}

void SelectCommand::auto_cmd_mode_cmd_callback(const tablet_socket_msgs::mode_cmd::ConstPtr& input_msg)
{
  if(command_mode_ == CommandMode::AUTO)
  {
    //TODO:check this if statement
    if(input_msg->mode == -1 || input_msg->mode == 0){
      reset_select_cmd_msg();
    }
    select_cmd_msg_.header.frame_id = input_msg->header.frame_id;
    select_cmd_msg_.header.stamp = input_msg->header.stamp;
    select_cmd_msg_.header.seq++;
    select_cmd_msg_.mode = input_msg->mode; 
    select_cmd_pub_.publish(select_cmd_msg_);
  }
}

void SelectCommand::auto_cmd_gear_cmd_callback(const tablet_socket_msgs::gear_cmd::ConstPtr& input_msg)
{
  if(command_mode_ == CommandMode::AUTO)
  {
    select_cmd_msg_.gear = input_msg->gear;
    select_cmd_pub_.publish(select_cmd_msg_);
  }
}

void SelectCommand::auto_cmd_accel_cmd_callback(const runtime_manager::accel_cmd::ConstPtr& input_msg)
{
  if(command_mode_ == CommandMode::AUTO)
  {
    select_cmd_msg_.header.frame_id = input_msg->header.frame_id;
    select_cmd_msg_.header.stamp = input_msg->header.stamp;
    select_cmd_msg_.header.seq++;
    select_cmd_msg_.accel = input_msg->accel;
    select_cmd_pub_.publish(select_cmd_msg_);
  }
}

void SelectCommand::auto_cmd_steer_cmd_callback(const runtime_manager::steer_cmd::ConstPtr& input_msg)
{
  if(command_mode_ == CommandMode::AUTO)
  {
    select_cmd_msg_.header.frame_id = input_msg->header.frame_id;
    select_cmd_msg_.header.stamp = input_msg->header.stamp;
    select_cmd_msg_.header.seq++;
    select_cmd_msg_.steer = input_msg->steer;
    select_cmd_pub_.publish(select_cmd_msg_);
  }
}

void SelectCommand::auto_cmd_brake_cmd_callback(const runtime_manager::brake_cmd::ConstPtr& input_msg)
{
  if(command_mode_ == CommandMode::AUTO)
  {
    select_cmd_msg_.header.frame_id = input_msg->header.frame_id;
    select_cmd_msg_.header.stamp = input_msg->header.stamp;
    select_cmd_msg_.header.seq++;
    select_cmd_msg_.brake = input_msg->brake;
    select_cmd_pub_.publish(select_cmd_msg_);
  }
}

void SelectCommand::auto_cmd_ctrl_cmd_callback(const waypoint_follower_msgs::ControlCommandStamped::ConstPtr& input_msg)
{
  if(command_mode_ == CommandMode::AUTO)
  {
    select_cmd_msg_.header.frame_id = input_msg->header.frame_id;
    select_cmd_msg_.header.stamp = input_msg->header.stamp;
    select_cmd_msg_.header.seq++;
    select_cmd_msg_.linear_velocity = input_msg->cmd.linear_velocity;
    select_cmd_msg_.steering_angle = input_msg->cmd.steering_angle;
    select_cmd_pub_.publish(select_cmd_msg_);
  }
}


int main(int argc, char** argv)
{
  ros::init(argc, argv, "select_command");
  ros::NodeHandle nh;
  ros::NodeHandle private_nh("~");

  SelectCommand select_command(nh, private_nh);

  ros::spin();
  return 0;
}

