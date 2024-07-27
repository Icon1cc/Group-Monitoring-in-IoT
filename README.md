### Project Title: Group Monitoring in Mobile IoT using ContikiNG and Node-RED

#### Introduction
This project focuses on developing a system for monitoring and analyzing group dynamics in a mobile Internet of Things (IoT) environment. Utilizing ContikiNG for managing IoT devices and Node-RED for backend processing, the system tracks the interactions between individuals carrying IoT devices, identifying group formations and alterations. This real-time monitoring can have applications in various fields, including smart city planning, event management, and social behavior analysis.

#### Project Objective
The main objective is to create an IoT-based solution to detect and monitor groups of people in a given area, analyze the group dynamics, and report significant changes to a backend system for further analysis.

#### System Overview
- **IoT Devices:** Individuals carry IoT devices that use radio signals as proximity sensors to detect the presence of other devices within a 1-hop distance.
- **Group Dynamics:** When at least three individuals are in mutual contact, a "group" is formed. Changes in the group, such as the addition or departure of members, are tracked in real-time.
- **Backend System:** A Node-RED based system processes the data from IoT devices, manages notifications, and computes statistics about each group.

#### Technology Stack
- **ContikiNG:** Used for programming the IoT devices, enabling them to detect proximity and communicate with each other and the backend system.
- **Node-RED:** Handles backend processing, including receiving notifications from IoT devices, data analysis, and generating statistical reports.

#### Implementation Details
1. **IoT Device Configuration:** 
   - Each device is equipped with ContikiNG OS.
   - Devices use radio signals to detect other devices within a 1-hop range.

2. **Group Detection and Monitoring:**
   - A group is formed when a device detects at least two other devices within range.
   - Group changes are monitored in real-time, with updates sent to the backend upon any change in group composition.

3. **Backend Processing with Node-RED:**
   - Node-RED receives data about group formations and alterations.
   - The backend system computes and stores statistics about groups, such as formation time, duration, and size variations.

4. **Statistical Analysis and Reporting:**
   - Analysis includes calculating the lifetime of each group and the average, maximum, and minimum size.
   - The backend periodically generates reports and insights based on the collected data.

#### Usage Scenarios
This system can be used in various scenarios like monitoring crowd dynamics in events, analyzing social interactions in public spaces, or enhancing safety and security protocols by tracking group movements in real-time.

#### Future Enhancements
- Integrate advanced machine learning algorithms for predictive analysis of group behaviors.
- Expand the system for larger scale implementations, considering challenges like device management and data scalability.

#### Conclusion
This project leverages the capabilities of ContikiNG and Node-RED to provide a comprehensive solution for real-time group monitoring in IoT environments, opening avenues for extensive applications in urban development, social studies, and event management.
