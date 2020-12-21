//
// Created by John Beeler on 4/28/18.
// Modified by Tim Pletcher on 31-Oct-2020.
//

#include "tiltHydrometer.h"
#include "jsonConfigHandler.h"

tiltHydrometer::tiltHydrometer(uint8_t color) {
    m_loaded            = false;
    m_color             = color;
    temp                = 0;
    gravity             = 0;
    m_lastUpdate        = 0;

    version_code        = 0;  // Set if captured - only applies to Gen 3/Pro Tilts
    weeks_since_last_battery_change = 0;  // Not currently implemented - for future use
    tilt_pro            = false;
    receives_battery    = false;

} // tiltHydrometer




uint8_t tiltHydrometer::uuid_to_color_no(std::string uuid) {

    if(uuid == TILT_COLOR_RED_UUID) {
        return TILT_COLOR_RED;
    } else if(uuid == TILT_COLOR_GREEN_UUID) {
        return TILT_COLOR_GREEN;
    } else if(uuid == TILT_COLOR_BLACK_UUID) {
        return TILT_COLOR_BLACK;
    } else if(uuid == TILT_COLOR_PURPLE_UUID) {
        return TILT_COLOR_PURPLE;
    } else if(uuid == TILT_COLOR_ORANGE_UUID) {
        return TILT_COLOR_ORANGE;
    } else if(uuid == TILT_COLOR_BLUE_UUID) {
        return TILT_COLOR_BLUE;
    } else if(uuid == TILT_COLOR_YELLOW_UUID) {
        return TILT_COLOR_YELLOW;
    } else if(uuid == TILT_COLOR_PINK_UUID) {
        return TILT_COLOR_PINK;
    } else {
        return TILT_NONE;
    }
}


std::string tiltHydrometer::color_name() {

    switch(m_color) {
        case TILT_COLOR_RED:
            return "Red";
        case TILT_COLOR_GREEN:
            return "Green";
        case TILT_COLOR_BLACK:
            return "Black";
        case TILT_COLOR_PURPLE:
            return "Purple";
        case TILT_COLOR_ORANGE:
            return "Orange";
        case TILT_COLOR_BLUE:
            return "Blue";
        case TILT_COLOR_YELLOW:
            return "Yellow";
        case TILT_COLOR_PINK:
            return "Pink";
        default:
            return "None";
    }
}

uint32_t tiltHydrometer::text_color() {

    switch(m_color) {
        case TILT_COLOR_RED:
            return 0xF800;
        case TILT_COLOR_GREEN:
            return 0x07E0;
        case TILT_COLOR_BLACK:
            return 0xFFFF;
        case TILT_COLOR_PURPLE:
            return 0x780F;
        case TILT_COLOR_ORANGE:
            return 0xFDA0;
        case TILT_COLOR_BLUE:
            return 0x001F;
        case TILT_COLOR_YELLOW:
            return 0xFFE0;
        case TILT_COLOR_PINK:
            return 0xFE19;
        default:
            return 0xFFFF;
    }
}


std::string tiltHydrometer::gsheets_beer_name() {
    switch(m_color) {
        case TILT_COLOR_RED:
            return app_config.config["sheetName_red"];
        case TILT_COLOR_GREEN:
            return app_config.config["sheetName_green"];
        case TILT_COLOR_BLACK:
            return app_config.config["sheetName_black"];
        case TILT_COLOR_PURPLE:
            return app_config.config["sheetName_purple"];
        case TILT_COLOR_ORANGE:
            return app_config.config["sheetName_orange"];
        case TILT_COLOR_BLUE:
            return app_config.config["sheetName_blue"];
        case TILT_COLOR_YELLOW:
            return app_config.config["sheetName_yellow"];
        case TILT_COLOR_PINK:
            return app_config.config["sheetName_pink"];
        default:
            return "";
    }
}


bool tiltHydrometer::set_values(uint16_t i_temp, uint16_t i_grav, uint8_t i_tx_pwr){
    double d_temp;
    double d_grav;
    float grav_scalar;
    if (tilt_pro) {
        grav_scalar = 10000.0f;
    }
    else {
        grav_scalar = 1000.0f;
    }

    //Serial.print("Raw value: ");
    //Serial.println(i_grav);

    // Implementation of a simple exponential smoothing filter to provide some averaging of 
    // gravity values received from the sensor between display updates / data reporting.

    if (!m_loaded) {
        //First pass through after loading tilt, filter_accumulator value must be initalized.
        filter_accumulator = i_grav;
    }
    else{
        // Effective smoothing filter constant is alpha / alphaScale
        // Ratio must be between 0 - 1 and lower values provide more smoothing.
        int alpha = 5;  

        int alphaScale = 10;
        i_grav = (alpha * i_grav + (alphaScale - alpha) * (filter_accumulator * alphaScale)/alphaScale +alphaScale / 2) / alphaScale;
        filter_accumulator = i_grav;
    }

    //Serial.print("Filtered value: ");
    //Serial.println(i_grav);

    if(i_grav >= 5000)  // If we received a gravity over 5000 then this Tilt is high resolution (Tilt Pro)
        tilt_pro = true;

    if(i_tx_pwr == 197)  // If we received a tx_pwr of 197 this Tilt sends its battery in the tx_pwr field
        receives_battery = true;
    else if(receives_battery)  // Its not 197 but we receive battery - set the battery value to tx_pwr
        weeks_since_last_battery_change = i_tx_pwr;

    if(tilt_pro) {
        // For Tilt Pros we have to divide the temp by 10 and the gravity by 10000
        d_temp = (double) i_temp / 10.0;
        d_grav = (double) i_grav / grav_scalar;
    } else {
        // For regular Tilts we take the temp as-is and divide the gravity by 1000
        d_temp = (double) i_temp;
        d_grav = (double) i_grav / grav_scalar;
    }

    nlohmann::json cal_params;

#if DEBUG_PRINTS
    Serial.print("Tilt gravity = ");
    Serial.println(d_grav);
#endif

    if(i_temp==999) {
        // If the temp is 999, that's an indicator that the SG actually represents the firmware version of the Tilt.
        // Although we won't do anything with this data, let's capture it.

        // This also has the (desired) side effect of not logging the 999 "temperature" and 1.00x "gravity"
        version_code = i_grav;
        return true;
    }


    if (app_config.config["applyCalibration"]) {
        double x0 = 0.0;
        double x1 = 1.0;
        double x2 = 0.0;
        double x3 = 0.0;

        switch(m_color) {
            case TILT_COLOR_RED:
                cal_params = app_config.config["cal_red"];
                break;
            case TILT_COLOR_GREEN:
                cal_params = app_config.config["cal_green"];
                break;
            case TILT_COLOR_BLACK:
                cal_params = app_config.config["cal_black"];
                break;
            case TILT_COLOR_PURPLE:
                cal_params = app_config.config["cal_purple"];
                break;
            case TILT_COLOR_ORANGE:
                cal_params = app_config.config["cal_orange"];
                break;
            case TILT_COLOR_BLUE:
                cal_params = app_config.config["cal_blue"];
                break;
            case TILT_COLOR_YELLOW:
                cal_params = app_config.config["cal_yellow"];
                break;
            case TILT_COLOR_PINK:
                cal_params = app_config.config["cal_pink"];
                break;
        }

        for (auto& el : cal_params.items()) {
            std::string coeff = el.key();
            double val = el.value().get<double>();
#if DEBUG_PRINTS
            Serial.print("Calibration coefficient ");
            Serial.print(coeff.c_str());
            Serial.print(" = ");
            Serial.println(val);
#endif
            if (!coeff.compare("x0")) x0 = val;
            if (!coeff.compare("x1")) x1 = val;
            if (!coeff.compare("x2")) x2 = val;
            if (!coeff.compare("x3")) x3 = val;
         }

         d_grav = x0 + x1 * d_grav + x2 * d_grav * d_grav + x3 * d_grav * d_grav * d_grav;

#if DEBUG_PRINTS
        Serial.print("Calibrated gravity = ");
        Serial.println(d_grav);
#endif
    }

    if (app_config.config["tempCorrect"]) {
        double ref_temp = 60.0;
        d_grav = d_grav * ((1.00130346 - 0.000134722124 * d_temp + 0.00000204052596 * d_temp * d_temp - 0.00000000232820948 * d_temp * d_temp * d_temp) / (1.00130346 - 0.000134722124 * ref_temp + 0.00000204052596 * ref_temp * ref_temp - 0.00000000232820948 * ref_temp * ref_temp * ref_temp));

#if DEBUG_PRINTS
        Serial.print("Temperature corrected gravity = ");
        Serial.println(d_grav);
#endif
    }




    gravity = (int) round(d_grav * grav_scalar);
    temp = i_temp;

    m_loaded = true;  // Setting loaded true now that we have gravity/temp values
    m_lastUpdate = xTaskGetTickCount();
    return true;
}

std::string tiltHydrometer::converted_gravity() {
    char rnd_gravity[7];
    if(tilt_pro)
        snprintf(rnd_gravity, 7,"%.4f",(float) gravity / 10000);
    else
        snprintf(rnd_gravity, 7,"%.4f",(float) gravity / 1000);
    std::string output = rnd_gravity;
    return output;
}

nlohmann::json tiltHydrometer::to_json() {
    nlohmann::json j;
    j = {
            {"color", color_name()},
            {"temp", converted_temp()},
            {"tempUnit", is_celsius() ? "C" : "F"},
            {"gravity", converted_gravity()},
            {"gsheets_name", gsheets_beer_name()},
            {"weeks_on_battery", weeks_since_last_battery_change},
    };
    return j;
}

std::string tiltHydrometer::converted_temp() {
    char rnd_temp[5];
    double d_temp;

    if(tilt_pro)  // For Tilt Pros we have to divide the temp by 10
        d_temp = (double) temp / 10.0;
    else  // For regular Tilts we take the temp as-is
        d_temp = (double) temp;

    if(is_celsius())
        d_temp = (d_temp - 32) * 5 / 9;

    snprintf(rnd_temp, 5,"%.1f", d_temp);

    std::string output = rnd_temp;
    return output;
}

bool tiltHydrometer::is_celsius() const {
    return app_config.config["tempUnit"] == "C";   
}

bool tiltHydrometer::is_loaded() {
    // Expire loading after 5 minutes
    if (m_loaded) {
        if ((xTaskGetTickCount() - m_lastUpdate) >= TILT_NO_DATA_RECEIVED_EXPIRATION) {
            m_loaded = false;
        }
    }
    return m_loaded;
}
