# Smart Air System
Smart Air System is an IoT project based on ESP32 designed to monitor ambient air quality in real time using the ISPU (Air Pollution Standard Index) as the main monitoring and evaluation standard. The system reads data from air quality sensors, processes it using edge AI directly on the device, enriches it with external API data, and presents ISPU-based insights and recommendations through a web-based dashboard. This project is built from a real-world use case to support better awareness and decision-making regarding environmental air conditions.

## Technology
- Web-based Dashboard (Streamlit)
- ESP32 as edge device
- Edge AI (on-device inference)
- On-The-Air (OTA) firmware update
- WiFi Manager for network configuration
- MQTT (HiveMQ) as communication protocol
- External Data Extension via third-party APIs (used as additional input for AI inference analysis) aqicn.org

## Features
* Real-time air quality sensor data acquisition
* ISPU-based air quality monitoring and classification
* Edge AI inference running directly on ESP32
* Air quality level analysis and recommendations based on ISPU
* **Separated dashboards for different stakeholders:**
  * Institutional dashboard for authorities and decision makers
  * Public dashboard for general users
* OLED display for on-device status and results
* Remote firmware update using OTA       

## System Workflow
1. Sensors collect air quality data in real time (PM, gas concentration, etc.)
2. Sensor data is processed and pre-validated on the ESP32
3. External API retrieves additional data from the server
4. Sensor data is converted and evaluated using ISPU standards
5. Sensor data and API data are combined as inputs for the AI model
6. The AI model performs inference on the ESP32 to determine ISPU-based air quality conditions
7. While inference is being processed, the OLED display and web dashboard show the latest sensor readings
8. Final inference results are displayed on the OLED screen
9. Inference results and sensor data are sent via MQTT and visualized on two separate web dashboards (institutional and public)

## What I Learned
- How to deploy and run edge AI models on ESP32
- Techniques to improve AI model performance on resource-limited devices
- Validating sensor input data directly in firmware
- Implementing ISPU-based air quality evaluation in an embedded system
- Combining sensor data with external API data for AI inference (data fusion)
- Designing separated dashboards for institutional and public users
- Building an end-to-end IoT system from real-world use cases

## How Can This Project Be Improved?
- Adding a solar power system for energy efficiency
- Integrating with CCTV cameras for security purposes
- Further improving AI model accuracy and robustness

## How to Run the Project
1. Clone this repository

   ```bash
   git clone https://github.com/username/smart-air-system.git
   cd smart-air-system
   ```
2. Set up MQTT topics using HiveMQ

   * Create a broker and topics
   * Configure MQTT settings in both ESP32 firmware and dashboard
3. Run the Streamlit dashboards

   ```bash
   streamlit run dashboard_public.py
   streamlit run dashboard_institution.py
   ```

   ```bash
   streamlit run app.py
   ```
