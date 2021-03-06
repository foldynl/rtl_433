
/*
 * *** Fine Offset WH1080 Weather Station ***
 * (aka Watson W-8681)
 * (aka Digitech XC0348 Weather Station)
 * (aka PCE-FWS 20) 
 * (aka Elecsa AstroTouch 6975)
 * (aka Froggit WH1080)
 * (aka .....)
 *
 * This module is based on Stanisław Pitucha ('viraptor' https://github.com/viraptor ) earl code for the Digitech XC0348 
 * Weather Station, which seems to be a rebranded Fine Offset WH1080 Weather Station. 
 *
 * Some info and code derived from Kevin Sangelee's page: 
 * http://www.susa.net/wordpress/2012/08/raspberry-pi-reading-wh1081-weather-sensors-using-an-rfm01-and-rfm12b/ .
 *
 * See also Frank 'SevenW' page ( https://www.sevenwatt.com/main/wh1080-protocol-v2-fsk/ ) for some other useful info.
 *
 * I only have re-elaborated and merged their works. Credits (and kudos) should go to them all (and to many others too).
 *
 *********************
 *
 * This weather station is based on an indoor touchscreen receiver, and on a 5+1 outdoor wireless sensors group 
 * (rain, wind speed, wind direction, temperature, humidity, plus a DCF77 time signal decoder, maybe capable to decode 
 * some other time signal standard).
 * See the product page here: http://www.foshk.com/weather_professional/wh1080.htm . 
 * It's a very popular weather station, you can easily find it on eBay or Amazon (just do a search for 'WH1080').
 *
 * The module seems to work fine, decoding all of the data as read into the original console (there is some minimal difference
 * sometime on the decimals due to the different architecture of the console processor, which is a little less precise).
 * 
 * Please note that the pressure sensor (barometer) is enclosed in the indoor console unit, NOT in the outdoor 
 * wireless sensors group. 
 * That's why it's NOT possible to get pressure data by wireless communication. If you need pressure data you should try 
 * an Arduino/Raspberry solution wired with a BMP180 or BMP085 sensor.
 *
 * Data are trasmitted in a 48 seconds cycle (data packet, then wait 48 seconds, then data packet...).
 * 
 * This module is also capable to decode the DCF77 time signal sent by the wireless time signal decoder: 
 * around the minute 59 of the even hours the sensor's TX stops sending weather data, probably to receive (and sync with) 
 * DCF77 signal.
 * After around 3-4 minutes of silence it starts to send just time data for some minute, then it starts again with 
 * weather data as usual.
 *
 * To recognize message type (weather or time) you can use the 'msg_type' field on json output:
 * msg_type 0 = weather data
 * msg_type 1 = time data
 *
 * By living in Europe I can only test DCF77 time decoding, so if you live outside Europe and you find garbage instead 
 * of correct time,
 * you should disable time decoding (or, better, try to implement a more complete time decoding system :) ).
 *
 * The 'Total rainfall' field is a cumulative counter, increased by 0.3 millimeters of rain at once.
 *
 * The station comes in three TX operating frequency versions: 433, 868.3 and 915 Mhz. 
 * I've had tested the module with a 'Froggit WH1080' on 868.3 Mhz, using '-f 868140000' as frequency parameter and 
 * it works fine 
 * (compiled in x86, RaspberryPi 1 (v2) and RaspberryPi 2, and also on a BananaPi platform. Everything is OK). 
 * I don't know if it works also with other versions and, generally speaking, with ALL of the rebranded versions of 
 * this weather station. 
 * I guess it *should* do... Just give it a try! :)
 *
 * 
 * ***TODO***: check if negative temperature values (and sign) are OK (no real winter this year where I live, so cannot test...) .
 * 
 *
 * 2016 Nicola Quiriti ('ovrheat')
 *
 *
 */
 

#include "data.h"
#include "rtl_433.h"
#include "util.h"
#include "math.h"

#define CRC_POLY 0x31
#define CRC_INIT 0xff



unsigned short msg_type = 0; // 0=Weather   1=Time


static const char* wind_dir_string[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",};
static const char* wind_dir_degr[]= {"0", "23", "45", "68", "90", "113", "135", "158", "180", "203", "225", "248", "270", "293", "315", "338",};

static unsigned short get_device_id(const uint8_t* br) {
	return (br[1] << 4 & 0xf0 ) | (br[2] >> 4);
}

static char* get_battery(const uint8_t* br) {  // Not enabled - Still unknown if it's right
	if ((br[9] >> 4) == 0) {
		return "OK";
	} else {
		return "LOW";
	}	
}
	
// ------------ WEATHER SENSORS DECODING ----------------------------------------------------

static float get_temperature(const uint8_t* br) {
    const int temp_raw = (br[2] << 8) + br[3];
    return ((temp_raw & 0x0fff) - 0x190) / 10.0;
}

static int get_humidity(const uint8_t* br) {
    return br[4];
}

static const char* get_wind_direction_str(const uint8_t* br) {
    return wind_dir_string[br[9] & 0x0f];
}

static const char* get_wind_direction_deg(const uint8_t* br) {
    return wind_dir_degr[br[9] & 0x0f];
}

static float get_wind_speed_raw(const uint8_t* br) {
    return br[5]; // Raw
}

static float get_wind_avg_ms(const uint8_t* br) {
    return (br[5] * 34.0f) / 100; // Meters/sec.
}

static float get_wind_avg_mph(const uint8_t* br) {
    return ((br[5] * 34.0f) / 100) * 2.23693629f; // Mph
}

static float get_wind_avg_kmh(const uint8_t* br) {
    return ((br[5] * 34.0f) / 100) * 3.6f; // Km/h
}

static float get_wind_avg_knot(const uint8_t* br) {
    return ((br[5] * 34.0f) / 100) * 1.94384f; // Knots
}

static float get_wind_gust_raw(const uint8_t* br) {
    return br[6]; // Raw
}

static float get_wind_gust_ms(const uint8_t* br) {
    return (br[6] * 34.0f) / 100; // Meters/sec.
}

static float get_wind_gust_mph(const uint8_t* br) {
    return ((br[6] * 34.0f) / 100) * 2.23693629f; // Mph
	
}

static float get_wind_gust_kmh(const uint8_t* br) {
    return ((br[6] * 34.0f) / 100) * 3.6f; // Km/h
}

static float get_wind_gust_knot(const uint8_t* br) {
    return ((br[6] * 34.0f) / 100) * 1.94384f; // Knots
}

static float get_rainfall(const uint8_t* br) {
	return ((((unsigned short)br[7] & 0x0f) << 8) | br[8]) * 0.3f;
}


//----------------- TIME DECODING ----------------------------------------------------

static int get_hours(const uint8_t* br) {
	return ((br[3] >> 4 & 0x03) * 10) + (br[3] & 0x0F);
}

static int get_minutes(const uint8_t* br) {
	return (((br[4] & 0xF0) >> 4) * 10) + (br[4] & 0x0F);
}

static int get_seconds(const uint8_t* br) {
	return (((br[5] & 0xF0) >> 4) * 10) + (br[5] & 0x0F);
}

static int get_year(const uint8_t* br) {
	return (((br[6] & 0xF0) >> 4) * 10) + (br[6] & 0x0F);
}
	
static int get_month(const uint8_t* br) {
	return ((br[7] >> 4 & 0x01) * 10) + (br[7] & 0x0F);	
}

static int get_day(const uint8_t* br) {
	return (((br[8] & 0xF0) >> 4) * 10) + (br[8] & 0x0F);
}

//-------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------



static int fineoffset_wh1080_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);

    if (bitbuffer->num_rows != 1) {
        return 0;
    }
    if (bitbuffer->bits_per_row[0] != 88) {
        return 0;
    }

    const uint8_t *br = bitbuffer->bb[0];

    if (br[0] != 0xff) {
        // preamble missing
        return 0;
    }

    if (br[10] != crc8(br, 10, CRC_POLY, CRC_INIT)) {
        // crc mismatch
        return 0;
    }
	
	if (br[0] == 0xff && br[1] == 0xa0) {
	msg_type = 0;
	} else if (br[0] == 0xff && br[1] == 0xb0) {
	msg_type = 1;
	}
	


	
//---------------------------------------------------------------------------------------	
//-------- GETTING WEATHER SENSORS DATA -------------------------------------------------
	
    const float temperature = get_temperature(br);
    const int humidity = get_humidity(br);
    const char* direction_str = get_wind_direction_str(br);
	const char* direction_deg = get_wind_direction_deg(br);	
	
	
	// Select which metric system for *wind avg speed* and *wind gust* :
	
	// Wind average speed :
	
	//const float speed = get_wind_avg_ms((br)   // <--- Data will be shown in Meters/sec.
	//const float speed = get_wind_avg_mph((br)  // <--- Data will be shown in Mph
	const float speed = get_wind_avg_kmh(br);  // <--- Data will be shown in Km/h
	//const float speed = get_wind_avg_knot((br) // <--- Data will be shown in Knots
	
	
	// Wind gust speed :
	
    //const float gust = get_wind_gust_ms(br);   // <--- Data will be shown in Meters/sec.
	//const float gust = get_wind_gust_mph(br);  // <--- Data will be shown in Mph
	const float gust = get_wind_gust_kmh(br);  // <--- Data will be shown in km/h
	//const float gust = get_wind_gust_knot(br); // <--- Data will be shown in Knots	
	
    const float rain = get_rainfall(br);
    const int device_id = get_device_id(br);
	const char* battery = get_battery(br);

//---------------------------------------------------------------------------------------	
//-------- GETTING TIME DATA ------------------------------------------------------------

	const int the_hours = get_hours(br);
	const int the_minutes =	get_minutes(br);
	const int the_seconds = get_seconds(br);
	const int the_year = 2000 + get_year(br);
	const int the_month = get_month(br);
	const int the_day = get_day(br);
	

//--------- PRESENTING DATA --------------------------------------------------------------
	
if (msg_type == 0) {
	
    data = data_make("time", 		"", 		DATA_STRING, time_str,
                     "model", 		"", 		DATA_STRING, "Fine Offset WH1080 weather station",
		     "msg_type",      "Msg type",	DATA_INT,    msg_type,	
                     "id",            "StationID",	DATA_FORMAT, "%04X",	DATA_INT,    device_id,
                     "temperature_C", "Temperature",	DATA_FORMAT, "%.01f C",	DATA_DOUBLE, temperature,
                     "humidity",      "Humidity",	DATA_FORMAT, "%u %%",	DATA_INT,    humidity,
                     "direction_str", "Wind string",	DATA_STRING, direction_str,
		     "direction_deg", "Wind degrees",	DATA_STRING, direction_deg,
                     "speed",         "Wind avg speed",	DATA_FORMAT, "%.02f",	DATA_DOUBLE, speed,
                     "gust",          "Wind gust",	DATA_FORMAT, "%.02f",	DATA_DOUBLE, gust,
                     "rain",          "Total rainfall",	DATA_FORMAT, "%.01f",	DATA_DOUBLE, rain,
					 //"battery",	  	  "Battery",		DATA_STRING, battery, // Unsure about Battery byte...
                     NULL);
    data_acquired_handler(data);
    return 1; 
	} else {
		
	data = data_make("time",          "",               DATA_STRING,	time_str,
                     "model",		 "",              DATA_STRING,	"Fine Offset WH1080 weather station",
		     "msg_type",	"Msg type",	  DATA_INT,		msg_type,	
                     "id",              "StationID",      DATA_FORMAT,	"%04X",	DATA_INT,	device_id,
                     "hours",		"Hours",	  DATA_FORMAT,	"%02d",	DATA_INT,	the_hours,
                     "minutes",		"Minutes",        DATA_FORMAT,	"%02d",	DATA_INT,	the_minutes,
                     "seconds",		"Seconds", 	  DATA_FORMAT,	"%02d",	DATA_INT,	the_seconds,
		     "year",		"Year", 	  DATA_FORMAT,	"%02d",	DATA_INT,	the_year,
                     "month",		"Month",     	  DATA_FORMAT,	"%02d",	DATA_INT,	the_month,
                     "day",		"Day",      	  DATA_FORMAT,	"%02d",	DATA_INT,	the_day,
                     NULL);
    data_acquired_handler(data);
    return 1; 
	}	
}

static char *output_fields[] = {
	"time",
	"model",
	"id",
	"temperature_C",
	"humidity",
	"direction_str",
	"direction_deg",
	"speed",
	"gust",
	"rain",
	"msg_type",
	"hours",
	"minutes",
	"seconds",
	"year",
	"month",
	"day",
	//"battery", // Still unsure about Battery byte(s)...
	NULL
};

r_device fineoffset_wh1080 = {
    .name           = "Fine Offset WH1080 Weather Station",
    .modulation     = OOK_PULSE_PWM_RAW,
    .short_limit    = 976,
    .long_limit     = 2400,
    .reset_limit    = 10520,
    .json_callback  = &fineoffset_wh1080_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields,
};
