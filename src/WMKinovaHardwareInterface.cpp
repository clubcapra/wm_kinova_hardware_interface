
//
// Created by philippe on 03/05/17.
//

#include "WMKinovaHardwareInterface.h"

#include <std_msgs/Float32.h>
#include <iostream>

using namespace wm_kinova_hardware_interface;
//using namespace wm_admittance;

namespace
{
    const uint PERIOD = 6000000;
}

// << ---- S T A T I C   V A R I A B L E   I N I T I A L I Z A T I O N ---- >>
bool WMKinovaHardwareInterface::KinovaReady = false;
bool WMKinovaHardwareInterface::KinovaLoaded = false;
double WMKinovaHardwareInterface::LastSentTime = 0;
double WMKinovaHardwareInterface::LastGatherTime = 0;
double WMKinovaHardwareInterface::Current = 0;
double WMKinovaHardwareInterface::Voltage = 0;
bool WMKinovaHardwareInterface::FreeIndex[6];
double WMKinovaHardwareInterface::Pos[6];
double WMKinovaHardwareInterface::Vel[6];
double WMKinovaHardwareInterface::Eff[6];
double WMKinovaHardwareInterface::Cmd[6];
double WMKinovaHardwareInterface::Offset[6];
double WMKinovaHardwareInterface::Temperature[6];
hardware_interface::VelocityJointInterface WMKinovaHardwareInterface::joint_velocity_interface_;
hardware_interface::JointStateInterface    WMKinovaHardwareInterface::joint_state_interface_;
TrajectoryPoint WMKinovaHardwareInterface::pointToSend;
ros::Publisher WMKinovaHardwareInterface::StatusPublisher;
KinovaDevice WMKinovaHardwareInterface::devices[MAX_KINOVA_DEVICE];

//WMAdmittance* WMKinovaHardwareInterface::aAdmittance;

bool WMKinovaHardwareInterface::StatusMonitorOn = true;
bool WMKinovaHardwareInterface::Simulation = false;

IndexByJointNameMapType WMKinovaHardwareInterface::aIndexByJointNameMap;

// << ---- H I G H   L E V E L   I N T E R F A C E ---- >>

bool WMKinovaHardwareInterface::init(ros::NodeHandle &root_nh, ros::NodeHandle &robot_hw_nh)
{
    if (!KinovaLoaded)
    {
        KinovaLoaded = true;
        KinovaLoaded = InitKinova();
        KinovaReady = true;
    }

    RetrieveDevices();

    for (int i = 0; i < 16; i++)
    {
        FreeIndex[i] = true;
    }
    pointToSend.InitStruct();
    // Set general kinova Points parameters.
    pointToSend.Limitations.accelerationParameter1 = 100;
    pointToSend.Limitations.accelerationParameter2 = 100;
    pointToSend.Limitations.accelerationParameter3 = 100;
    pointToSend.Limitations.speedParameter1 = 100;
    pointToSend.Limitations.speedParameter2 = 100;
    pointToSend.Limitations.speedParameter3 = 100;
    pointToSend.SynchroType = 0;
    pointToSend.LimitationsActive = 0;
    pointToSend.Limitations.speedParameter1 = 60;
    pointToSend.Limitations.speedParameter2 = 60;
    pointToSend.Limitations.speedParameter3 = 60;
    pointToSend.Position.Type = ANGULAR_VELOCITY;

    Name = "";
    Index = 0;
    std::vector<std::string> Joints;

    // Mandatory parameters
    if (!robot_hw_nh.getParam("joints", Joints)) {
        return false;
    }
    Name = Joints[0];
    if (!robot_hw_nh.getParam("index", Index)) {
        return false;
    }
    if (!robot_hw_nh.getParam("offset", Offset[Index])) {
        return false;
    }

    aIndexByJointNameMap.emplace(Index, Name);

    if (!robot_hw_nh.getParam("complience_level", ComplienceLevel)){
        ComplienceLevel = 1;
    }
    if (!robot_hw_nh.getParam("complience_threshold", ComplienceThreshold)){
        ComplienceThreshold = 100;
    }
    if (!robot_hw_nh.getParam("complience_derivation_factor", ComplienceDerivationFactor)){
        ComplienceThreshold = 0.01;
    }
    if (!robot_hw_nh.getParam("complience_loss_factor", ComplienceLossFactor)){
        ComplienceThreshold = 0.75;
    }
    if (!robot_hw_nh.getParam("complience_resistance", ComplienceResistance)){
        ComplienceResistance = 0.5;
    }
    if (!robot_hw_nh.getParam("speed_ratio", SpeedRatio)){
        SpeedRatio = 1;
    }

//    aAdmittance = wm_admittance::WMAdmittance::getInstance();

    cmd = 0;
    pos = 0;
    vel = 0;
    eff = 0;
    FreeIndex[Index] = false;

    joint_state_interface_.registerHandle(hardware_interface::JointStateHandle(Name, &pos, &vel, &eff));
    joint_velocity_interface_.registerHandle(hardware_interface::JointHandle(joint_state_interface_.getHandle(Name), &cmd));
    registerInterface(&joint_state_interface_);
    registerInterface(&joint_velocity_interface_);

    //TemperaturePublisher = nh.advertise<diagnostic_msgs::DiagnosticArray>("diagnostics", 100);

    GatherInfo();
    seff = Eff[Index];
    deff = seff;

    return true;
}

void WMKinovaHardwareInterface::read(const ros::Time &time, const ros::Duration &period) {

    GetInfos();
    //std::cout << "\nIndex = " << Index << ", Position = " << Pos[Index] << ", Effort = " << Eff[Index];
    diagnostic_msgs::DiagnosticArray dia_array;

    pos = AngleProxy( 0, Pos[Index]);
    eff = Eff[Index];

//    diagnostic_msgs::DiagnosticStatus dia_status;
//    dia_status.name = "kinova_arm";
//    dia_status.hardware_id = Name;
//
//    diagnostic_msgs::KeyValue KV1;
//    KV1.key = "temperature";
//    char chare[50];
//    std::sprintf(chare, "%lf", Temperature[Index]);
//    KV1.value = chare;
//
//    diagnostic_msgs::KeyValue KV2;
//    KV2.key = "torque";
//    std::sprintf(chare, "%lf", Eff[Index]);
//    KV2.value = chare;
//
//    dia_status.values.push_back(KV1);
//    dia_status.values.push_back(KV2);
//
//    dia_array.status.push_back(dia_status);
//
//    TemperaturePublisher.publish(dia_array);
}

void WMKinovaHardwareInterface::write(const ros::Time &time, const ros::Duration &period)
{
    double cmdVel;
//    if (aAdmittance->isAdmittanceEnabled()) {
//        cmdVel = aAdmittance->getAdmittanceVelocityFromJoint(aIndexByJointNameMap[Index]) + cmd * 57.295779513;
//    }
//    else {
        cmdVel = cmd * 57.295779513;
        seff += (eff-seff)*ComplienceLossFactor;
        deff += (seff-deff)*ComplienceDerivationFactor;

        if ((seff-deff)*(seff-deff)>ComplienceThreshold) {
            cmdVel += (-2*seff+deff)*ComplienceLevel;
        }
        else {
            deff += (seff-deff)*ComplienceResistance;
        }
//    }

    //if (!aAdmittance->isAdmittanceEnabled())
    //{
    //    seff += (eff-seff)*ComplienceLossFactor;
//
    //    deff += (seff-deff)*ComplienceDerivationFactor;
//
    //    if ((seff-deff)*(seff-deff)>ComplienceThreshold){
    //        cmdVel += (-2*seff+deff)*ComplienceLevel;
    //    } else {
    //        deff += (seff-deff)*ComplienceResistance;
    //    }
    //}

    SetVel(Index, cmdVel*SpeedRatio); // from r/s to ded/p

}

// << ---- M E D I U M   L E V E L   I N T E R F A C E ---- >>
bool WMKinovaHardwareInterface::GetInfos() {
    double Now = ros::Time::now().toNSec();
    bool result;  // true = no error
    if (LastGatherTime < Now - PERIOD) {
        LastGatherTime = Now;
        result = GatherInfo();
        if (!result) {
            ROS_ERROR("Kinova Hardware Interface.  error detected while trying to gather information");
            return false;
        }
    }
    return true;
}

bool WMKinovaHardwareInterface::SetVel(int Index, double cmd) {
    double Now = ros::Time::now().toNSec();
    bool result = true;  // true = no error
    Cmd[Index] = cmd;
    if (LastSentTime < Now - PERIOD) {
        LastSentTime = Now;
        result = SendPoint();
        if (!result) {
            ROS_ERROR("Kinova Hardware Interface.  error detected while trying to send point");
        }
    }
    return result;
}

// << ---- L O W   L E V E L   I N T E R F A C E ---- >>
bool WMKinovaHardwareInterface::InitKinova() noexcept
{
    bool kinovaInitialized = true;

    ROS_INFO("\"* * *            C H A R G E M E N T   D E   K I N O V A   A P I           * * *\"");
    try
    {
        if (!WMKinovaApiWrapper::isInitialized())
        {
            WMKinovaApiWrapper::initialize(); // Can throw
        }
    }
    catch (const std::exception& exception)
    {
        kinovaInitialized = false;
        ROS_ERROR("Exception was raised when attempting to initialize kinova API.");
        ROS_ERROR("Reason: %s", exception.what());
    }
    return kinovaInitialized;
}

bool WMKinovaHardwareInterface::RetrieveDevices()
{

    bool Success = false;
    int nb_attempts = 2;
    int result;
    ROS_INFO("\"* * *              R E C H E R C H E   D U   B R A S             * * *\"");
    while (!Success) {

        result = (*WMKinovaApiWrapper::MyInitAPI)();
        WMKinovaApiWrapper::MyGetDevices(devices, result);
        if (result != 1) {
            if (nb_attempts > 4) {
                ROS_INFO("\"* * *                   B R A S   I N T R O U V E                * * *\"");
                ROS_INFO("\"* * *          M O D E   S I M U L A T I O N   A C T I V E       * * *\"");
                Simulation = true;
                Success = true;
            } else {
                ROS_INFO("\"* * *             B R A S   I N T R O U V A B L E                * * *\"");
                ROS_INFO("\"* * *                 T E N T A T I V E   #%d/8                   * * *\"",
                         nb_attempts);
                nb_attempts++;
                sleep(1);
            }
        } else {
            Success = true;
            ROS_INFO("\"* * *      I N I T I A L I S A T I O N   T E R M I N E E         * * *\"");
            ROS_INFO("\"* * *                  B R A S   T R O U V E                     * * *\"");
        }
    }
    LastGatherTime = ros::Time::now().toNSec();
    return true;  // TODO  detect errors
}

bool WMKinovaHardwareInterface::StartStatusMonitoring(int argc, char **argv) {
    StatusMonitorOn = true;
    std::string NodeName = "kinova status";
    ros::init(argc, argv, NodeName);
    ros::NodeHandle n;
    StatusPublisher = n.advertise<diagnostic_msgs::DiagnosticArray>("diagnostics", 100);
    ros::spinOnce();
    return true;
}

bool WMKinovaHardwareInterface::GatherInfo() {

    if (KinovaReady) {
        if (Simulation) {
            // Do crude simulation
            for (int i = 0; i < 6; i++) {
                Temperature[i] = 0.1234;
                Pos[i] += Vel[i] / 50000;
                Eff[i] += 1;
            }
            Current = -1;
            Voltage = -1;
        } else {
            AngularPosition PositionList;
            WMKinovaApiWrapper::MyGetAngularCommand(PositionList);
            Pos[0] = PositionList.Actuators.Actuator1 / 160 * M_PI - Offset[0];
            Pos[1] = PositionList.Actuators.Actuator2 / 180 * M_PI - Offset[1];
            Pos[2] = PositionList.Actuators.Actuator3 / 180 * M_PI - Offset[2];
            Pos[3] = PositionList.Actuators.Actuator4 / 180 * M_PI - Offset[3];
            Pos[4] = PositionList.Actuators.Actuator5 / 180 * M_PI - Offset[4];
            Pos[5] = PositionList.Actuators.Actuator6 / 180 * M_PI - Offset[5];

            SensorsInfo SI;
            WMKinovaApiWrapper::MyGetSensorsInfo(SI);
            Temperature[0] = SI.ActuatorTemp1;
            Temperature[1] = SI.ActuatorTemp2;
            Temperature[2] = SI.ActuatorTemp3;
            Temperature[3] = SI.ActuatorTemp4;
            Temperature[4] = SI.ActuatorTemp5;
            Temperature[5] = SI.ActuatorTemp6;
            Current = SI.Current;
            Voltage = SI.Voltage;

            AngularPosition ForceList;
            WMKinovaApiWrapper::MyGetAngularForce(ForceList);
            Eff[0] = ForceList.Actuators.Actuator1;
            Eff[1] = ForceList.Actuators.Actuator2;
            Eff[2] = ForceList.Actuators.Actuator3;
            Eff[3] = ForceList.Actuators.Actuator4;
            Eff[4] = ForceList.Actuators.Actuator5;
            Eff[5] = ForceList.Actuators.Actuator6;

        }
        if (StatusMonitorOn) {

            diagnostic_msgs::DiagnosticArray dia_array2;

            diagnostic_msgs::DiagnosticStatus dia_status;
            dia_status.name = "kinova_arm";
            dia_status.hardware_id = "kinova_arm";

            diagnostic_msgs::KeyValue KV1;
            KV1.key = "current";
            char chare[50];
            std::sprintf(chare, "%lf", Current);
            KV1.value = chare;

            diagnostic_msgs::KeyValue KV2;
            KV2.key = "voltage";
            std::sprintf(chare, "%lf", Voltage);
            KV2.value = chare;

            dia_status.values.push_back(KV1);
            dia_status.values.push_back(KV2);

            dia_array2.status.push_back(dia_status);

            //StatusPublisher.publish(dia_array2);

        }
    }
    return true;  // TODO  detect errors
}

bool WMKinovaHardwareInterface::SendPoint() {

    if (KinovaReady) {
        for (int i = 0; i < 6; i++)
        {
            Vel[i] = Cmd[i];
        }
        if (Simulation) {
            // Do crude simulation
            for (int i = 0; i < 6; i++) {
                Temperature[i] = 0;
            }
        } else {
            //  execute order
            pointToSend.Position.Actuators.Actuator1 = (float) Cmd[0];
            pointToSend.Position.Actuators.Actuator2 = (float) Cmd[1];
            pointToSend.Position.Actuators.Actuator3 = (float) Cmd[2];
            pointToSend.Position.Actuators.Actuator4 = (float) Cmd[3];
            pointToSend.Position.Actuators.Actuator5 = (float) Cmd[4];
            pointToSend.Position.Actuators.Actuator6 = (float) Cmd[5];

            WMKinovaApiWrapper::MyEraseAllTrajectories();
            //ROS_INFO( "Send!" );
            //ROS_INFO("S1=%lf", Vel[0] );
            //ROS_INFO("S2=%lf", pointToSend.Position.Actuators.Actuator1 );
            WMKinovaApiWrapper::MySendAdvanceTrajectory(pointToSend);
        }
    }
    return true;  // TODO  detect errors
}
PLUGINLIB_EXPORT_CLASS( wm_kinova_hardware_interface::WMKinovaHardwareInterface, hardware_interface::RobotHW)
