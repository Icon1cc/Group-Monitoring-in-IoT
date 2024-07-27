# Group Monitoring in Mobile IoT

## Project Overview
This project demonstrates a system where IoT devices carried by individuals detect proximity using radio signals. The system tracks the formation and dissolution of groups when individuals come into contact. It also reports group changes to a back-end system and calculates statistics such as group lifetime, and average, maximum, and minimum group sizes over time.

### Technologies Used
- **Contiki-NG**: An open-source operating system for the Internet of Things.
- **Node-RED**: A flow-based development tool for visual programming.

## Table of Contents
1. [Introduction](#introduction)
    1. [Description of the Project](#description-of-the-project)
    2. [Assumptions and Guidelines](#assumptions-and-guidelines)
    3. [Technologies and Implementation](#technologies-and-implementation)
2. [Overall Structure](#overall-structure)
    1. [COOJA](#cooja)
    2. [RPL Border-Router](#rpl-border-router)
    3. [UDP Signaler](#udp-signaler)
    4. [MQTT-UDP Mote](#mqtt-udp-mote)
3. [Implementation](#implementation)
    1. [Frontend (Contiki-NG - COOJA)](#frontend-contiki-ng-cooja)
    2. [Backend (Node-RED)](#backend-node-red)
4. [Results](#results)
5. [Conclusion and Future Work](#conclusion-and-future-work)

## Introduction

### Description of the Project
This project implements a system where IoT devices carried by individuals detect proximity using radio signals. When devices come within range, it identifies "in contact" individuals and forms "groups" of three or more people. Group changes are reported to the back-end system, which updates group statistics, including lifetime and membership details.

### Assumptions and Guidelines
- IoT devices are assumed to be reachable across multiple hops via a static IPv6 border router.
- The COOJA simulator is used for testing.
- Group membership is monitored periodically, with a timeout mechanism for handling departures.

### Technologies and Implementation
The project integrates **Contiki-NG** and **Node-RED**. Contiki-NG handles the IoT device interactions, while Node-RED manages back-end data processing and statistics calculation.

## Overall Structure

### COOJA
COOJA simulator is used to test IoT device interactions. It supports complex tests without memory constraints and uses the Constant Loss Unit-Disk Graph Model for reliable message transmission.

### RPL Border-Router
Acts as the root of the network, connecting all wireless sensors to the internet, ensuring smooth data transmission.

### UDP Signaler
Manages dynamic interactions among IoT devices, transmitting signals to update the network about group changes.

### MQTT-UDP Mote
Handles both MQTT and UDP communications, ensuring efficient data transmission between the backend system and IoT devices.

## Implementation

### Frontend (Contiki-NG - COOJA)
- **UDP Client Process**: Sends UDP packets to discover neighboring nodes and update the contact list.
- **MQTT Client Process**: Manages MQTT client connections, subscriptions, and message publishing.
- **Contact Management Functions**: Maintain the contact list and mutual contacts between IoT devices.
- **Group Formation and Reporting Functions**: Report group formation to the backend via MQTT messages.
- **Configuration Functions**: Initialize and update MQTT client configurations and topics.
- **Helper Functions**: Assist in formatting MQTT messages, IP address manipulation, and JSON array construction.

### Backend (Node-RED)
- **Group Cardinality Updates**: Updates the number of active group members.
- **Lifetime Tracking**: Calculates the lifetime of groups.
- **Cardinality Statistics**: Tracks statistical data for each group.
- **Periodic Monitoring**: Checks for inactive members and updates group statistics.
- **Timeout Management**: Filters out inactive members.
- **Group Dismantling**: Dismantles groups that fall below the minimum member count.
- **Member Activity Updates**: Updates the last active time for group members.
- **Membership Changes**: Updates changes in group membership.
- **New Group Creation**: Creates new groups for senders not in existing groups.
- **Array Comparison**: Checks for changes in group memberships.
- **Group Survivability Check**: Dismantles groups with insufficient members.
- **Main Execution Flow**: Manages group memberships and logs dismantled groups.
- **Message Handling**: Processes incoming messages and updates group data.
- **Context Management**: Maintains group data across function executions.

## Results
### Cooja Simulation
Shows IoT devices connected and reporting group formations to the backend. The console logs confirm the correct implementation of group formation logic.

### Node-RED Flow
Configures the MQTT broker and processes incoming data to compute and display group statistics.

## Conclusion and Future Work
The project successfully demonstrated real-time monitoring and management of mobile IoT devices using Contiki-NG and Node-RED. Future improvements include:
- **Scalability and Performance Optimization**: Enhance the system to handle more devices efficiently.
- **Advanced Mobility Models**: Implement sophisticated mobility models for varied real-world scenarios.
- **Enhanced Security Features**: Add stronger security measures to protect data.
- **User Interface (UI) Enhancements**: Develop a comprehensive UI for better system monitoring and management.

## Authors
- **Rishabh Tiwari** - [rishtiwari98@gmail.com](mailto:rishtiwari98@gmail.com)
- **Alexandre Boving** - [alexandre.boving@gmail.com](mailto:alexandre.boving@gmail.com)
