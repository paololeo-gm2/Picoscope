/*******************************************************************************
 *
 * Filename: ps5000aCon.c
 *
 * Description:
 *   This is a console mode program that demonstrates how to use some of 
 *	 the PicoScope 5000 Series (ps5000a) driver API functions to perform operations
 *	 using a PicoScope 5000 Series Flexible Resolution Oscilloscope.
 *
 *	Supported PicoScope models:
 *
 *		PicoScope 5242A/B/D & 5442A/B/D
 *		PicoScope 5243A/B/D & 5443A/B/D
 *		PicoScope 5244A/B/D & 5444A/B/D
 *
 * Examples:
 *   Collect a block of samples immediately
 *   Collect a block of samples when a trigger event occurs
 *	 Collect a block of samples using Equivalent Time Sampling (ETS)
 *   Collect samples using a rapid block capture with trigger
 *   Collect a stream of data immediately
 *   Collect a stream of data when a trigger event occurs
 *   Set Signal Generator, using standard or custom signals
 *   Change timebase & voltage scales
 *   Display data in mV or ADC counts
 *	 Handle power source changes
 *
 *	To build this application:-
 *
 *		If Microsoft Visual Studio (including Express/Community Edition) is being used:
 *
 *			Select the solution configuration (Debug/Release) and platform (x86/x64)
 *			Ensure that the 32-/64-bit ps5000a.lib can be located
 *			Ensure that the ps5000aApi.h and PicoStatus.h files can be located
 *
 *		Otherwise:
 *
 *			 Set up a project for a 32-/64-bit console mode application
 *			 Add this file to the project
 *			 Add ps5000a.lib to the project (Microsoft C only)
 *			 Add ps5000aApi.h and PicoStatus.h to the project
 *			 Build the project
 *
 *  Linux platforms:
 *
 *		Ensure that the libps5000a driver package has been installed using the
 *		instructions from https://www.picotech.com/downloads/linux
 *
 *		Place this file in the same folder as the files from the linux-build-files
 *		folder. In a terminal window, use the following commands to build
 *		the ps5000aCon application:
 *
 *			./autogen.sh <ENTER>
 *			make <ENTER>
 *
 * Copyright (C) 2013-2018 Pico Technology Ltd. See LICENSE file for terms.
 *
 ******************************************************************************/

#include <stdio.h>

/* Headers for Windows */
#ifdef _WIN32
#include "windows.h"
#include <conio.h>
#include "ps5000aApi.h"
#else
#include <sys/types.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include <libps5000a/ps5000aApi.h>
#ifndef PICO_STATUS
#include <libps5000a/PicoStatus.h>
#endif

#define Sleep(a) usleep(1000*a)
#define scanf_s scanf
#define fscanf_s fscanf
#define memcpy_s(a,b,c,d) memcpy(a,c,d)

typedef enum enBOOL{FALSE,TRUE} BOOL;

/* A function to detect a keyboard press on Linux */
int32_t _getch()
{
        struct termios oldt, newt;
        int32_t ch;
        int32_t bytesWaiting;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~( ICANON | ECHO );
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        setbuf(stdin, NULL);
        do {
                ioctl(STDIN_FILENO, FIONREAD, &bytesWaiting);
                if (bytesWaiting)
                        getchar();
        } while (bytesWaiting);

        ch = getchar();

        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return ch;
}

int32_t _kbhit()
{
        struct termios oldt, newt;
        int32_t bytesWaiting;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~( ICANON | ECHO );
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        setbuf(stdin, NULL);
        ioctl(STDIN_FILENO, FIONREAD, &bytesWaiting);

        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return bytesWaiting;
}

int32_t fopen_s(FILE ** a, const int8_t * b, const int8_t * c)
{
FILE * fp = fopen(b,c);
*a = fp;
return (fp>0)?0:-1;
}

/* A function to get a single character on Linux */
#define max(a,b) ((a) > (b) ? a : b)
#define min(a,b) ((a) < (b) ? a : b)
#endif

int32_t cycles = 0;

#define BUFFER_SIZE 	2000

#define QUAD_SCOPE		4
#define DUAL_SCOPE		2

#define MAX_PICO_DEVICES 64
#define TIMED_LOOP_STEP 500

typedef struct
{
	int16_t DCcoupled;
	int16_t range;
	int16_t enabled;
	float analogueOffset;
}CHANNEL_SETTINGS;

typedef enum
{
	MODEL_NONE = 0,
	MODEL_PS5242A = 0xA242,
	MODEL_PS5242B = 0xB242,
	MODEL_PS5243A = 0xA243,
	MODEL_PS5243B = 0xB243,
	MODEL_PS5244A = 0xA244,
	MODEL_PS5244B = 0xB244,
	MODEL_PS5442A = 0xA442,
	MODEL_PS5442B = 0xB442,
	MODEL_PS5443A = 0xA443,
	MODEL_PS5443B = 0xB443,
	MODEL_PS5444A = 0xA444,
	MODEL_PS5444B = 0xB444
} MODEL_TYPE;

typedef enum
{
	SIGGEN_NONE = 0,
	SIGGEN_FUNCTGEN = 1,
	SIGGEN_AWG = 2
} SIGGEN_TYPE;

typedef struct tPwq
{
	PS5000A_CONDITION * pwqConditions;
	int16_t nPwqConditions;
	PS5000A_DIRECTION * pwqDirections;
	int16_t nPwqDirections;
	uint32_t lower;
	uint32_t upper;
	PS5000A_PULSE_WIDTH_TYPE type;
}PWQ;

typedef struct data
{
	int time;
	int ADC_chA;
	int mV_chA;
	int ADC_chB;
	int mV_chB;
} data;


typedef struct
{
	int16_t handle;
	MODEL_TYPE				model;
	int8_t						modelString[8];
	int8_t						serial[10];
	int16_t						complete;
	int16_t						openStatus;
	int16_t						openProgress;
	PS5000A_RANGE			firstRange;
	PS5000A_RANGE			lastRange;
	int16_t						channelCount;
	int16_t						maxADCValue;
	SIGGEN_TYPE				sigGen;
	int16_t						hasHardwareETS;
	uint16_t					awgBufferSize;
	CHANNEL_SETTINGS	channelSettings [PS5000A_MAX_CHANNELS];
	PS5000A_DEVICE_RESOLUTION	resolution;
	int16_t						digitalPortCount;
}UNIT;

uint32_t	timebase = 8;
BOOL			scaleVoltages = TRUE;

uint16_t inputRanges [PS5000A_MAX_RANGES] = {
												10,
												20,
												50,
												100,
												200,
												500,
												1000,
												2000,
												5000,
												10000,
												20000,
												50000};

int16_t			g_autoStopped;
int16_t   	g_ready = FALSE;
uint64_t 		g_times [PS5000A_MAX_CHANNELS];
int16_t     g_timeUnit;
int32_t     g_sampleCount;
uint32_t		g_startIndex;
int16_t			g_trig = 0;
uint32_t		g_trigAt = 0;
int16_t			g_overflow = 0;

int8_t blockFile[20]  = "block.txt";

int8_t binaryFile[20] = "block_binary.txt";

int8_t streamFile[20] = "stream.txt";

typedef struct tBufferInfo
{
	UNIT * unit;
	int16_t **driverBuffers;
	int16_t **appBuffers;

} BUFFER_INFO;

/****************************************************************************
* Callback
* used by ps5000a data block collection calls, on receipt of data.
* used to set global flags etc checked by user routines
****************************************************************************/
void PREF4 callBackBlock( int16_t handle, PICO_STATUS status, void * pParameter)
{
	if (status != PICO_CANCELLED)
	{
		g_ready = TRUE;
	}
}

/****************************************************************************
* callbackStreaming
* Used by ps5000a data streaming collection calls, on receipt of data.
* Used to set global flags etc checked by user routines
****************************************************************************/
void PREF4 callBackStreaming(	int16_t handle,
	int32_t noOfSamples,
	uint32_t startIndex,
	int16_t overflow,
	uint32_t triggerAt,
	int16_t triggered,
	int16_t autoStop,
	void	*pParameter)
{
	int32_t channel;
	BUFFER_INFO * bufferInfo = NULL;

	if (pParameter != NULL)
	{
		bufferInfo = (BUFFER_INFO *) pParameter;
	}

	// used for streaming
	g_sampleCount = noOfSamples;
	g_startIndex  = startIndex;
	g_autoStopped = autoStop;

	// flag to say done reading data
	g_ready = TRUE;

	// flags to show if & where a trigger has occurred
	g_trig = triggered;
	g_trigAt = triggerAt;

	g_overflow = overflow;

	if (bufferInfo != NULL && noOfSamples)
	{
		for (channel = 0; channel < bufferInfo->unit->channelCount; channel++)
		{
			if (bufferInfo->unit->channelSettings[channel].enabled)
			{
				if (bufferInfo->appBuffers && bufferInfo->driverBuffers)
				{
					// Max buffers
					if (bufferInfo->appBuffers[channel * 2]  && bufferInfo->driverBuffers[channel * 2])
					{
						memcpy_s (&bufferInfo->appBuffers[channel * 2][startIndex], noOfSamples * sizeof(int16_t),
							&bufferInfo->driverBuffers[channel * 2][startIndex], noOfSamples * sizeof(int16_t));
					}

					// Min buffers
					if (bufferInfo->appBuffers[channel * 2 + 1] && bufferInfo->driverBuffers[channel * 2 + 1])
					{
						memcpy_s (&bufferInfo->appBuffers[channel * 2 + 1][startIndex], noOfSamples * sizeof(int16_t),
							&bufferInfo->driverBuffers[channel * 2 + 1][startIndex], noOfSamples * sizeof(int16_t));
					}
				}
			}
		}
	}
}

/****************************************************************************
* SetDefaults - restore default settings
****************************************************************************/
void setDefaults(UNIT * unit)
{
	PICO_STATUS status;
	PICO_STATUS powerStatus;
	int32_t i;

	status = ps5000aSetEts(unit->handle, PS5000A_ETS_OFF, 0, 0, NULL);					// Turn off hasHardwareETS
	printf(status?"setDefaults:ps5000aSetEts------ 0x%08lx \n":"", status);

	powerStatus = ps5000aCurrentPowerSource(unit->handle);

	for (i = 0; i < unit->channelCount; i++) // reset channels to most recent settings
	{
		if(i >= DUAL_SCOPE && powerStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
		{
			// No need to set the channels C and D if Quad channel scope and power not enabled.
		}
		else
		{
			status = ps5000aSetChannel(unit->handle, (PS5000A_CHANNEL)(PS5000A_CHANNEL_A + i),
				unit->channelSettings[PS5000A_CHANNEL_A + i].enabled,
				(PS5000A_COUPLING)unit->channelSettings[PS5000A_CHANNEL_A + i].DCcoupled,
				(PS5000A_RANGE)unit->channelSettings[PS5000A_CHANNEL_A + i].range, 
				unit->channelSettings[PS5000A_CHANNEL_A + i].analogueOffset);

			printf(status?"SetDefaults:ps5000aSetChannel------ 0x%08lx \n":"", status);

		}
	}
	
	status = ps5000aSetChannel(unit->handle, (PS5000A_CHANNEL)(PS5000A_CHANNEL_A + 4), TRUE, (PS5000A_COUPLING)FALSE, (PS5000A_RANGE)8, 0.0f);

	printf(status?"SetDefaults:ps5000aSetChannel------ 0x%08lx \n":"", status);
}

/****************************************************************************
* adc_to_mv
*
* Convert an 16-bit ADC count into millivolts
****************************************************************************/
int32_t adc_to_mv(int32_t raw, int32_t rangeIndex, UNIT * unit)
{
	return (raw * inputRanges[rangeIndex]) / unit->maxADCValue;
}

/****************************************************************************
* mv_to_adc
*
* Convert a millivolt value into a 16-bit ADC count
*
*  (useful for setting trigger thresholds)
****************************************************************************/
int16_t mv_to_adc(int16_t mv, int16_t rangeIndex, UNIT * unit)
{
	return (mv * unit->maxADCValue) / inputRanges[rangeIndex];
}

/****************************************************************************************
* ChangePowerSource - function to handle switches between +5V supply, and USB only power
* Only applies to PicoScope 544xA/B units 
******************************************************************************************/
PICO_STATUS changePowerSource(int16_t handle, PICO_STATUS status, UNIT * unit)
{
	int8_t ch;

	switch (status)
	{
		case PICO_POWER_SUPPLY_NOT_CONNECTED:		// User must acknowledge they want to power via USB
			do
			{
				printf("\n5 V power supply not connected.");
				printf("\nDo you want to run using USB only Y/N?\n");
				ch = toupper(_getch());
				
				if(ch == 'Y')
				{
					printf("\nPowering the unit via USB\n");
					status = ps5000aChangePowerSource(handle, PICO_POWER_SUPPLY_NOT_CONNECTED);		// Tell the driver that's ok
				
					if(status == PICO_OK && unit->channelCount == QUAD_SCOPE)
					{
						unit->channelSettings[PS5000A_CHANNEL_C].enabled = FALSE;
						unit->channelSettings[PS5000A_CHANNEL_D].enabled = FALSE;
					}
					else if (status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
					{
						status = changePowerSource(handle, status, unit);
					}
					else
					{
						// Do nothing
					}

				}
			}
			while(ch != 'Y' && ch != 'N');
			printf(ch == 'N'?"Please use the +5V power supply to power this unit\n":"");
			break;

		case PICO_POWER_SUPPLY_CONNECTED:
			printf("\nUsing +5 V power supply voltage.\n");
			status = ps5000aChangePowerSource(handle, PICO_POWER_SUPPLY_CONNECTED);		// Tell the driver we are powered from +5V supply
			break;

		case PICO_USB3_0_DEVICE_NON_USB3_0_PORT:
			do
			{
				printf("\nUSB 3.0 device on non-USB 3.0 port.");
				printf("\nDo you wish to continue Y/N?\n");
				ch = toupper(_getch());

				if (ch == 'Y')
				{
					printf("\nSwitching to use USB power from non-USB 3.0 port.\n");
					status = ps5000aChangePowerSource(handle, PICO_USB3_0_DEVICE_NON_USB3_0_PORT);		// Tell the driver that's ok

					if (status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
					{
						status = changePowerSource(handle, status, unit);
					}
					else
					{
						// Do nothing
					}

				}
			} while (ch != 'Y' && ch != 'N');
			printf(ch == 'N' ? "Please use a USB 3.0 port or press 'Y'.\n" : "");
			break;

		case PICO_POWER_SUPPLY_UNDERVOLTAGE:
			do
			{
				printf("\nUSB not supplying required voltage");
				printf("\nPlease plug in the +5 V power supply\n");
				printf("\nHit any key to continue, or Esc to exit...\n");
				ch = _getch();
				
				if (ch == 0x1B)	// ESC key
				{
					exit(0);
				}
				else
				{
					status = ps5000aChangePowerSource(handle, PICO_POWER_SUPPLY_CONNECTED);		// Tell the driver that's ok
				}
			}
			while (status == PICO_POWER_SUPPLY_REQUEST_INVALID);
			break;
	}

	printf("\n");
	return status;
}

/****************************************************************************
* ClearDataBuffers
*
* stops GetData writing values to memory that has been released
****************************************************************************/
PICO_STATUS clearDataBuffers(UNIT * unit)
{
	int32_t i;
	PICO_STATUS status;

	for (i = 0; i < unit->channelCount; i++) 
	{
		if(unit->channelSettings[i].enabled)
		{
			if ((status = ps5000aSetDataBuffers(unit->handle, (PS5000A_CHANNEL) i, NULL, NULL, 0, 0, PS5000A_RATIO_MODE_NONE)) != PICO_OK)
			{
				printf("clearDataBuffers:ps5000aSetDataBuffers(channel %d) ------ 0x%08lx \n", i, status);
			}
		}
	}
	return status;
}

/****************************************************************************
* streamDataHandler
* - Used by the two stream data examples - untriggered and triggered
* Inputs:
* - unit - the unit to sample on
* - preTrigger - the number of samples in the pre-trigger phase 
*					(0 if no trigger has been set)
***************************************************************************/
void streamDataHandler(UNIT * unit, uint32_t preTrigger)
{
	//Variabili utili
	int32_t i, j;
	uint32_t sampleCount = 50000; /* make sure overview buffer is large enough */
	FILE * fp = NULL;
	int16_t * buffers[2 * PS5000A_MAX_CHANNELS];
	int16_t * appBuffers[2 * PS5000A_MAX_CHANNELS];
	PICO_STATUS status;
	PICO_STATUS powerStatus;
	uint32_t sampleInterval;
	int32_t index = 0;
	int32_t totalSamples;
	uint32_t postTrigger;
	int16_t autostop;
	uint32_t downsampleRatio;
	uint32_t triggeredAt = 0;
	PS5000A_TIME_UNITS timeUnits;
	PS5000A_RATIO_MODE ratioMode;
	int16_t retry = 0;
	int16_t powerChange = 0;
	uint32_t numStreamingValues = 0;

	int num_of_samples = 0;
	BUFFER_INFO bufferInfo;

	//Non ci interessa
	powerStatus = ps5000aCurrentPowerSource(unit->handle);
	
	for (i = 0; i < unit->channelCount; i++) 
	{
		if (i >= DUAL_SCOPE && unit->channelCount == QUAD_SCOPE && powerStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
		{
			// No need to set the channels C and D if Quad channel scope and power supply not connected.
		}
		else
		{
			if (unit->channelSettings[i].enabled)
			{
				buffers[i * 2] = (int16_t*) calloc(sampleCount, sizeof(int16_t));
				buffers[i * 2 + 1] = (int16_t*) calloc(sampleCount, sizeof(int16_t));
			
				status = ps5000aSetDataBuffers(unit->handle, (PS5000A_CHANNEL)i, buffers[i * 2], buffers[i * 2 + 1], sampleCount, 0, PS5000A_RATIO_MODE_NONE);

				appBuffers[i * 2] = (int16_t*) calloc(sampleCount, sizeof(int16_t));
				appBuffers[i * 2 + 1] = (int16_t*) calloc(sampleCount, sizeof(int16_t));

				printf(status?"StreamDataHandler:ps5000aSetDataBuffers(channel %ld) ------ 0x%08lx \n":"", i, status);
			}
		}
	}
	
	downsampleRatio = 1;
	timeUnits = PS5000A_US;
	sampleInterval = 1;
	ratioMode = PS5000A_RATIO_MODE_NONE;
	preTrigger = 0;
	postTrigger = 1000000;
	autostop = TRUE;
	
	bufferInfo.unit = unit;	
	bufferInfo.driverBuffers = buffers;
	bufferInfo.appBuffers = appBuffers;

	if (autostop)
	{
		printf("\nStreaming Data for %lu samples", postTrigger / downsampleRatio);
		
		if (preTrigger)	// We pass 0 for preTrigger if we're not setting up a trigger
		{
			printf(" after the trigger occurs\nNote: %lu Pre Trigger samples before Trigger arms\n\n",preTrigger / downsampleRatio);
		}
		else
		{
			printf("\n\n");
		}
	}
	else
	{
		printf("\nStreaming Data continually.\n\n");
	}

	g_autoStopped = FALSE;


	do
	{
		retry = 0;

		status = ps5000aRunStreaming(unit->handle, &sampleInterval, timeUnits, preTrigger, postTrigger, autostop, 
										downsampleRatio, ratioMode, sampleCount);

		if (status != PICO_OK)
		{
			// PicoScope 5X4XA/B/D devices...+5 V PSU connected or removed or
			// PicoScope 524XD devices on non-USB 3.0 port
			if (status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED ||
						status == PICO_USB3_0_DEVICE_NON_USB3_0_PORT || status == PICO_POWER_SUPPLY_UNDERVOLTAGE)		
			{
				status = changePowerSource(unit->handle, status, unit);
				retry = 1;
			}
			else
			{
				printf("streamDataHandler:ps5000aRunStreaming ------ 0x%08lx \n", status);
				return;
			}
		}
	}
	while (retry);

	printf("Streaming data...Press a key to stop\n");

	
	fopen_s(&fp, streamFile, "w");

	if (fp != NULL)
	{
		fprintf(fp,"Streaming Data Log\n\n");
		fprintf(fp,"For each of the %d Channels, results shown are....\n",unit->channelCount);
		fprintf(fp,"Maximum Aggregated value ADC Count & mV, Minimum Aggregated value ADC Count & mV\n\n");

		for (i = 0; i < unit->channelCount; i++) 
		{
			if (unit->channelSettings[i].enabled) 
			{
				fprintf(fp,"   Max ADC    Max mV  Min ADC  Min mV   ");
			}
		}
		fprintf(fp, "\n");
	}
	

	totalSamples = 0;

	while (!_kbhit() && !g_autoStopped)
	{
		/* Poll until data is received. Until then, GetStreamingLatestValues wont call the callback */
		g_ready = FALSE;

		status = ps5000aGetStreamingLatestValues(unit->handle, callBackStreaming, &bufferInfo);

		// PicoScope 5X4XA/B/D devices...+5 V PSU connected or removed or
		// PicoScope 524XD devices on non-USB 3.0 port
		if (status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED ||
			status == PICO_USB3_0_DEVICE_NON_USB3_0_PORT || status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
		{
			if (status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
			{
				changePowerSource(unit->handle, status, unit);
			}

			printf("\n\nPower Source Change");
			powerChange = 1;
		}

		index ++;

		if (g_ready && g_sampleCount > 0) /* Can be ready and have no data, if autoStop has fired */
		{
			if (g_trig)
			{
				triggeredAt = totalSamples + g_trigAt;		// Calculate where the trigger occurred in the total samples collected
			}

			totalSamples += g_sampleCount;
			printf("\nCollected %3li samples, index = %5lu, Total: %6d samples ", g_sampleCount, g_startIndex, totalSamples);
			
			if (g_trig)
			{
				printf("Trig. at index %lu total %lu", g_trigAt, triggeredAt + 1);	// show where trigger occurred
				num_of_samples += 1;
			}
			
			for (i = g_startIndex; i < (int32_t)(g_startIndex + g_sampleCount); i++) 
			{
				
				if (fp != NULL)
				{
					for (j = 0; j < unit->channelCount; j++) 
					{
						if (unit->channelSettings[j].enabled) 
						{
							fprintf(	fp,
								"Ch%C  %5d = %+5dmV, %5d = %+5dmV   ",
								(char)('A' + j),
								appBuffers[j * 2][i],
								adc_to_mv(appBuffers[j * 2][i], unit->channelSettings[PS5000A_CHANNEL_A + j].range, unit),
								appBuffers[j * 2 + 1][i],
								adc_to_mv(appBuffers[j * 2 + 1][i], unit->channelSettings[PS5000A_CHANNEL_A + j].range, unit));
						}
					}

					fprintf(fp, "\n");
				}
				else
				{
					printf("Cannot open the file %s for writing.\n", streamFile);
				}
				
			}
		}
	}

	printf("\n\n");

	ps5000aStop(unit->handle);

	if (fp != NULL)
	{

		fclose (fp);
	}

	if (!g_autoStopped && !powerChange)  
	{
		printf("\nData collection aborted\n");
		_getch();
	}
	else
	{
		printf("\nData collection complete.\n\n");
	}
	
	for (i = 0; i < unit->channelCount; i++) 
	{
		if(unit->channelSettings[i].enabled)
		{
			free(buffers[i * 2]);
			free(appBuffers[i * 2]);

			free(buffers[i * 2 + 1]);
			free(appBuffers[i * 2 + 1]);
		}
	}

	clearDataBuffers(unit);
}

/****************************************************************************
* setTrigger
*
* - Used to call all the functions required to set up triggering.
*
***************************************************************************/
PICO_STATUS setTrigger(UNIT * unit,
	PS5000A_TRIGGER_CHANNEL_PROPERTIES_V2 * channelProperties,
	int16_t nChannelProperties,
	PS5000A_CONDITION * triggerConditions,
	int16_t nTriggerConditions,
	PS5000A_DIRECTION * directions,
	uint16_t nDirections,
	struct tPwq * pwq,
	uint32_t delay,
	uint64_t autoTriggerUs)
{
	PICO_STATUS status;
	PS5000A_CONDITIONS_INFO info = PS5000A_CLEAR;
	PS5000A_CONDITIONS_INFO pwqInfo = PS5000A_CLEAR;

	int16_t auxOutputEnabled = 0; // Not used by function call

	status = ps5000aSetTriggerChannelPropertiesV2(unit->handle, channelProperties, nChannelProperties, auxOutputEnabled);

	if (status != PICO_OK) 
	{
		printf("setTrigger:ps5000aSetTriggerChannelPropertiesV2 ------ Ox%08lx \n", status);
		return status;
	}

	if (nTriggerConditions != 0)
	{
		info = (PS5000A_CONDITIONS_INFO)(PS5000A_CLEAR | PS5000A_ADD); // Clear and add trigger condition specified unless no trigger conditions have been specified
	}

	status = ps5000aSetTriggerChannelConditionsV2(unit->handle, triggerConditions, nTriggerConditions, info);

	if (status != PICO_OK)
	{
		printf("setTrigger:ps5000aSetTriggerChannelConditionsV2 ------ 0x%08lx \n", status);
		return status;
	}

	status = ps5000aSetTriggerChannelDirectionsV2(unit->handle, directions, nDirections);

	if (status != PICO_OK) 
	{
		printf("setTrigger:ps5000aSetTriggerChannelDirectionsV2 ------ 0x%08lx \n", status);
		return status;
	}

	status = ps5000aSetAutoTriggerMicroSeconds(unit->handle, autoTriggerUs);

	if (status != PICO_OK)
	{
		printf("setTrigger:ps5000aSetAutoTriggerMicroSeconds ------ 0x%08lx \n", status);
		return status;
	}

	status = ps5000aSetTriggerDelay(unit->handle, delay);
	
	if (status != PICO_OK)
	{
		printf("setTrigger:ps5000aSetTriggerDelay ------ 0x%08lx \n", status);
		return status;
	}

	// Clear and add pulse width qualifier condition, clear if no pulse width qualifier has been specified
	if (pwq->nPwqConditions != 0)
	{
		pwqInfo = (PS5000A_CONDITIONS_INFO)(PS5000A_CLEAR | PS5000A_ADD);
	}

	status = ps5000aSetPulseWidthQualifierConditions(unit->handle, pwq->pwqConditions, pwq->nPwqConditions, pwqInfo);

	if (status != PICO_OK)
	{
		printf("setTrigger:ps5000aSetPulseWidthQualifierConditions ------ 0x%08lx \n", status);
		return status;
	}

	status = ps5000aSetPulseWidthQualifierDirections(unit->handle, pwq->pwqDirections, pwq->nPwqDirections);

	if (status != PICO_OK)
	{
		printf("setTrigger:ps5000aSetPulseWidthQualifierDirections ------ 0x%08lx \n", status);
		return status;
	}

	status = ps5000aSetPulseWidthQualifierProperties(unit->handle, pwq->lower, pwq->upper, pwq->type);

	if (status != PICO_OK)
	{
		printf("setTrigger:ps5000aSetPulseWidthQualifierProperties ------ Ox%08lx \n", status);
		return status;
	}

	return status;
}

/****************************************************************************
* collectRapidBlock
*  this function demonstrates how to collect a set of captures using
*  rapid block mode.
****************************************************************************/
void collectRapidBlock(UNIT * unit)
{
	int num_of_waveforms = 1000;
	int num_of_points = 2000;
	int num_of_points_pre_trigger = 500;
	int num_of_points_post_trigger = 1500;

	int16_t init_trigger_voltage = 500;
	PS5000A_CHANNEL init_trigger_channel = PS5000A_EXTERNAL;

	int8_t ch = '.';
	
	while(ch != 'S')
	{
		printf("\n\n");
		printf("ACTUAL OPTIONS FOR BLOCK DATA CAPTURE (DATA STRUCTURE)\n\n");
		printf("Number of waveforms = %i\n", num_of_waveforms);
		printf("Number of Points per waveform = %i\n", num_of_points);
		printf("Number of Points pre-trigger = %i\n", num_of_points_pre_trigger);
		printf("Number of Points post-trigger = %i\n", num_of_points_post_trigger);
		printf("\n");

		printf("ACTUAL OPTIONS FOR BLOCK DATA CAPTURE (TRIGGER OPTIONS)\n\n");
		switch (init_trigger_channel)
		{
			case PS5000A_CHANNEL_A:
				printf("Trigger Channel = A\n");
				break;
			case PS5000A_CHANNEL_B:
				printf("Trigger Channel = B\n");
				break;
			case PS5000A_CHANNEL_C:
				printf("Trigger Channel = C\n");
				break;
			case PS5000A_CHANNEL_D:
				printf("Trigger Channel = D\n");
				break;
			case PS5000A_EXTERNAL:
				printf("Trigger Channel = EXT\n");
				break;
			default:
				printf("Trigger Channel not found\n");
				break;
		}
		printf("Trigger Voltage = %i\n mV", init_trigger_voltage);

		printf("\n");
		printf("Please select operation:\n\n");

		printf("W - Set Number of Waveforms		P - Set Number of Points per waveform\n");
		printf("F - Set Number of Points pre-trigger	L - Set Number of points post-trigger\n");
		printf("\n");
		printf("C - Set Trigger channel 		V - Set Trigger Voltage\n");
		printf("\n");
		printf("S - Continue\n");
		printf("Operation:");

		ch = toupper(_getch());

		printf("\n\n");

		switch (ch)
		{
			case 'W':
				printf("Number of waveforms to collect:");
				scanf_s("%i", &num_of_waveforms);
				break;
			case 'P':
				printf("Number of points per waveform to collect:");
				scanf_s("%i", &num_of_points);
				do
				{
					printf("Number of points pre-trigger to collect:");
					scanf_s("%i", &num_of_points_pre_trigger);
					num_of_points_post_trigger = num_of_points - num_of_points_pre_trigger;
					if(num_of_points_pre_trigger > num_of_points || num_of_points_pre_trigger < 0)
					{
						printf("Invalid value: Number of points pre-trigger is greater than Number of points. Please set a valid value\n");
					}				
				} while(num_of_points_pre_trigger > num_of_points || num_of_points_pre_trigger < 0);
				break;
			case 'F':
				do
				{
					printf("Number of points pre-trigger to collect:");
					scanf_s("%i", &num_of_points_pre_trigger);
					num_of_points_post_trigger = num_of_points - num_of_points_pre_trigger;
					if(num_of_points_pre_trigger > num_of_points || num_of_points_pre_trigger < 0)
					{
						printf("Invalid value: Number of points pre-trigger is greater than Number of points. Please set a valid value\n");
					}
				}while (num_of_points_pre_trigger > num_of_points || num_of_points_pre_trigger < 0);
				break;
			case 'L': 
				do
				{
					printf("Number of points post-trigger to collect:");
					scanf_s("%i", &num_of_points_post_trigger);
					num_of_points_pre_trigger = num_of_points - num_of_points_post_trigger;
					if(num_of_points_post_trigger > num_of_points || num_of_points_post_trigger < 0)
					{
						printf("Invalid value: Number of points post-trigger is greater than Number of points. Please set a valid value\n");
					}
				}while (num_of_points_post_trigger > num_of_points || num_of_points_post_trigger < 0);
				break;
			case 'S':
				break;
			case 'C':
				do
				{
					printf("0 -> A\n");
					printf("1 -> B\n");
					printf("2 -> C\n");
					printf("3 -> D\n");
					printf("4 -> EXT\n");
					printf("\n");
					printf("Trigger Channel:");
					scanf_s("%i", &init_trigger_channel);	
					if(init_trigger_channel > 4 || init_trigger_channel < 0)
					{
						printf("Invalid value: Channel value out of range. Please set a valid value\n");
					}
				}while(init_trigger_channel > 4 || init_trigger_channel < 0);
				break;
			case 'V':
				do
				{
					printf("Trigger Voltage:");
					scanf_s("%hi", &init_trigger_voltage);
					if(init_trigger_voltage < 5000 || init_trigger_voltage > 5000) 
					{
						printf("Trigger Voltage out of range (over 5V). Please set a valid value\n");
					}
				}while(init_trigger_voltage < 5000 || init_trigger_voltage > 5000);
				break;
			default:
				printf("Invalid Operation\n");
				break;
		}

	}

	printf("\n\n");

	uint32_t	nCaptures;
	uint32_t	nSegments;
	int32_t		nMaxSamples;
	uint32_t	nSamples = num_of_points;
	int32_t		timeIndisposed;
	uint32_t	capture;
	int16_t		channel;
	int16_t***	rapidBuffers;
	int16_t*	overflow;
	PICO_STATUS status;
	int16_t		i;
	uint32_t	nCompletedCaptures;
	int16_t		retry;
	int32_t timeInterval;

	int16_t		triggerVoltage = 500; // mV
	//PS5000A_CHANNEL triggerChannel = PS5000A_CHANNEL_A;
	PS5000A_CHANNEL triggerChannel = PS5000A_EXTERNAL;
	int16_t		voltageRange = inputRanges[unit->channelSettings[triggerChannel].range];
	int16_t		triggerThreshold = 0;

	int32_t		timeIntervalNs = 0;
	int32_t		maxSamples = 0;
	uint32_t	maxSegments = 0;

	uint64_t timeStampCounterDiff = 0;
	
	FILE * fp = NULL;
	
	FILE * fbin = NULL;

	PS5000A_TRIGGER_INFO * triggerInfo; // Struct to store trigger timestamping information

	// Structures for setting up trigger - declare each as an array of multiple structures if using multiple channels
	struct tPS5000ATriggerChannelPropertiesV2 triggerProperties;
	struct tPS5000ACondition conditions;
	struct tPS5000ADirection directions;

	// Struct to hold Pulse Width Qualifier information
	struct tPwq pulseWidth;

	memset(&triggerProperties, 0, sizeof(struct tPS5000ATriggerChannelPropertiesV2));
	memset(&conditions, 0, sizeof(struct tPS5000ACondition));
	memset(&directions, 0, sizeof(struct tPS5000ADirection));
	memset(&pulseWidth, 0, sizeof(struct tPwq));

	// If the channel is not enabled, warn the User and return
	if (unit->channelSettings[triggerChannel].enabled == 0)
	{
		printf("collectBlockTriggered: Channel not enabled.");
	}

	// If the trigger voltage level is greater than the range selected, set the threshold to half
	// of the range selected e.g. for ±200 mV, set the threshold to 10 0mV
	
	if (triggerVoltage > voltageRange)
	{
		printf("Changing trigger voltage to half of the channel voltage range!\n");
		triggerVoltage = (voltageRange / 2);
	}
	

	triggerThreshold = mv_to_adc(triggerVoltage, unit->channelSettings[triggerChannel].range, unit);

	// Set trigger channel properties
	triggerProperties.thresholdUpper = triggerThreshold;
	triggerProperties.thresholdUpperHysteresis = 256 * 10;
	triggerProperties.thresholdLower = triggerThreshold;
	triggerProperties.thresholdLowerHysteresis = 256 * 10;
	triggerProperties.channel = triggerChannel;

	// Set trigger conditions
	conditions.source = triggerChannel;
	conditions.condition = PS5000A_CONDITION_TRUE;

	// Set trigger directions
	directions.source = triggerChannel;
	directions.direction = PS5000A_RISING;
	directions.mode = PS5000A_LEVEL;

	printf("Collect rapid block triggered...\n");
	printf("Collects when value rises past %d ", scaleVoltages ?
		adc_to_mv(triggerProperties.thresholdUpper, unit->channelSettings[PS5000A_EXTERNAL].range, unit)	// If scaleVoltages, print mV value
		: triggerProperties.thresholdUpper);																// else print ADC Count

	printf(scaleVoltages ? "mV\n" : "ADC Counts\n");
	printf("Press any key to abort\n");

	setDefaults(unit);

	// Trigger enabled
	status = setTrigger(unit, &triggerProperties, 1, &conditions, 1, &directions, 1, &pulseWidth, 0, 0);

	// Find the maximum number of segments
	status = ps5000aGetMaxSegments(unit->handle, &maxSegments);

	// Set the number of segments - this can be more than the number of waveforms to collect
	nSegments = num_of_waveforms;

	if (nSegments > maxSegments)
	{
		nSegments = maxSegments;
	}

	// Set the number of captures
	nCaptures = num_of_waveforms;

	// Segment the memory
	status = ps5000aMemorySegments(unit->handle, nSegments, &nMaxSamples);

	// Set the number of captures
	status = ps5000aSetNoOfCaptures(unit->handle, nCaptures);

	// Run
	//timebase = 127;		// 1 MS/s at 8-bit resolution, ~504 kS/s at 12 & 16-bit resolution

	// Verify timebase and number of samples per channel for segment 0
	do
	{
		status = ps5000aGetTimebase(unit->handle, timebase, nSamples, &timeIntervalNs, &maxSamples, 0);

		if (status == PICO_INVALID_TIMEBASE)
		{
			timebase++;
		}
	} while (status != PICO_OK);

	do
	{
		retry = 0;
		status = ps5000aRunBlock(unit->handle, num_of_points_pre_trigger, num_of_points_post_trigger, timebase, &timeIndisposed, 0, callBackBlock, NULL);

		if (status != PICO_OK)
		{
			// PicoScope 5X4XA/B/D devices...+5 V PSU connected or removed or
			// PicoScope 524XD devices on non-USB 3.0 port
			if (status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED || status == PICO_USB3_0_DEVICE_NON_USB3_0_PORT)
			{
				status = changePowerSource(unit->handle, status, unit);
				retry = 1;
			}
			else
			{
				printf("collectRapidBlock:ps5000aRunBlock ------ 0x%08lx \n", status);
			}
		}
	} while (retry);

	// Wait until data ready
	g_ready = 0;

	while (!g_ready && !_kbhit())
	{
		Sleep(0);
	}

	if (!g_ready)
	{
		_getch();
		status = ps5000aStop(unit->handle);
		status = ps5000aGetNoOfCaptures(unit->handle, &nCompletedCaptures);

		printf("Rapid capture aborted. %lu complete blocks were captured\n", nCompletedCaptures);
		printf("\nPress any key...\n\n");
		_getch();

		if (nCompletedCaptures == 0)
		{
			return;
		}

		// Only display the blocks that were captured
		nCaptures = (uint16_t)nCompletedCaptures;
	}

	// Allocate memory
	rapidBuffers = (int16_t ***)calloc(unit->channelCount, sizeof(int16_t*));
	overflow = (int16_t *)calloc(unit->channelCount * nCaptures, sizeof(int16_t));

	for (channel = 0; channel < unit->channelCount; channel++)
	{
		if (unit->channelSettings[channel].enabled)
		{
			rapidBuffers[channel] = (int16_t **)calloc(nCaptures, sizeof(int16_t*));
		}
	}

	for (channel = 0; channel < unit->channelCount; channel++)
	{
		if (unit->channelSettings[channel].enabled)
		{
			for (capture = 0; capture < nCaptures; capture++)
			{
				rapidBuffers[channel][capture] = (int16_t *)calloc(nSamples, sizeof(int16_t));
			}
		}
	}

	for (channel = 0; channel < unit->channelCount; channel++)
	{
		if (unit->channelSettings[channel].enabled)
		{
			for (capture = 0; capture < nCaptures; capture++)
			{
				status = ps5000aSetDataBuffer(unit->handle, (PS5000A_CHANNEL)channel, rapidBuffers[channel][capture], nSamples, capture, PS5000A_RATIO_MODE_NONE);
			}
		}
	}

	// Allocate memory for the trigger timestamping
	triggerInfo = (PS5000A_TRIGGER_INFO *)malloc(nCaptures * sizeof(PS5000A_TRIGGER_INFO));
	memset(triggerInfo, 0, nCaptures * sizeof(PS5000A_TRIGGER_INFO));

	// Get data
	status = ps5000aGetValuesBulk(unit->handle, &nSamples, 0, nCaptures - 1, 1, PS5000A_RATIO_MODE_NONE, overflow);

	if (status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED ||
				status == PICO_USB3_0_DEVICE_NON_USB3_0_PORT || status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
	{
		printf("\nPower Source Changed. Data collection aborted.\n");
	}

	// Retrieve trigger timestamping information
	status = ps5000aGetTriggerInfoBulk(unit->handle, triggerInfo, 0, nCaptures - 1);

	fopen_s(&fp, blockFile, "w");
	fopen_s(&fbin, binaryFile, "w");
		
	if (status == PICO_OK)
	{
		//print first 10 samples from each capture
		for (capture = 0; capture < nCaptures; capture++)
		{
			fprintf(fp, "Time (ns)\t");
			printf("\n");
			
			fprintf(fp,"ADC_chA\tmV_chA\tADC_chB\tmV_chB");
			
			fprintf(fp, "\n");

			printf("Capture index %d:-\n\n", capture);

			// Trigger Info status & Timestamp 
			printf("Trigger Info:- Status: %u  Trigger index: %u  Timestamp Counter: %I64u\n", triggerInfo[capture].status, triggerInfo[capture].triggerIndex, triggerInfo[capture].timeStampCounter);

			// Calculate time between trigger events - the first timestamp is arbitrary so is only used to calculate offsets

			// The structure containing the status code with bit flag PICO_DEVICE_TIME_STAMP_RESET will have an arbitrary timeStampCounter value. 
			// This should be the first segment in each run, so in this case segment 0 will be ignored.

			if (capture == 0)
			{
				// Nothing to display
				printf("\n");
			}
			else if (capture > 0 && triggerInfo[capture].status == PICO_OK)
			{
				timeStampCounterDiff = triggerInfo[capture].timeStampCounter - triggerInfo[capture - 1].timeStampCounter;
				printf("Time since trigger for last segment: %I64u ns\n\n", (timeStampCounterDiff * (uint64_t)timeIntervalNs));
			}
			else
			{
				// Do nothing
			}

			for (channel = 0; channel < unit->channelCount; channel++)
			{
				if (unit->channelSettings[channel].enabled)
				{
					printf("Channel %c:\t", 'A' + channel);
				}
			}

			printf("\n\n");

			for (i = 0; i < 10; i++)
			{
				for (channel = 0; channel < unit->channelCount; channel++)
				{
					if (unit->channelSettings[channel].enabled)
					{
						printf("   %6d       ", scaleVoltages ?
							adc_to_mv(rapidBuffers[channel][capture][i], unit->channelSettings[PS5000A_CHANNEL_A + channel].range, unit)	// If scaleVoltages, print mV value
							: rapidBuffers[channel][capture][i]);																	// else print ADC Count
					}
				}

				printf("\n");
			}
			for (i = 0; i < nSamples; i++)
			{
				struct data values;
				//fprintf(fp, "%I64u ", g_times[0] + (uint64_t)(i * timeInterval));
				fprintf(fp, "%i\t\t", g_times[0] + i*timeIntervalNs );
				
				values.time = g_times[0] + i*timeIntervalNs;
				
				for (int j = 0; j < 2; j++) 
				{
					if (unit->channelSettings[j].enabled) 
					{
						if(j==0) {
							values.ADC_chA = rapidBuffers[j][capture][i];
							values.mV_chA = adc_to_mv(rapidBuffers[j][capture][i], unit->channelSettings[PS5000A_CHANNEL_A + j].range, unit);
						}
						else if(j==1) {
							values.ADC_chB = rapidBuffers[j][capture][i];
							values.mV_chB = adc_to_mv(rapidBuffers[j][capture][i], unit->channelSettings[PS5000A_CHANNEL_A + j].range, unit);
						}
						fprintf(fp, "%6d\t%+6d\t", rapidBuffers[j][capture][i], adc_to_mv(rapidBuffers[j][capture][i], unit->channelSettings[PS5000A_CHANNEL_A + j].range, unit));
					}
				}
				fwrite(&values, sizeof(struct data), 1, fbin);

				fprintf(fp, "\n");
			}
		}
	}

	// Stop
	status = ps5000aStop(unit->handle);

	// Free memory
	free(overflow);

	for (channel = 0; channel < unit->channelCount; channel++)
	{
		if (unit->channelSettings[channel].enabled)
		{
			for (capture = 0; capture < nCaptures; capture++)
			{
				free(rapidBuffers[channel][capture]);
			}
		}
	}

	for (channel = 0; channel < unit->channelCount; channel++)
	{
		if (unit->channelSettings[channel].enabled)
		{
			free(rapidBuffers[channel]);
		}
	}

	free(rapidBuffers);
	free(triggerInfo);
	
	if (fp != NULL)
	{
		fclose(fp);
	}
	
	if (fbin != NULL)
	{
		fclose(fbin);
	}
}

/****************************************************************************
* Initialise unit' structure with Variant specific defaults
****************************************************************************/
void set_info(UNIT * unit)
{
	int8_t description [11][25]= { "Driver Version",
		"USB Version",
		"Hardware Version",
		"Variant Info",
		"Serial",
		"Cal Date",
		"Kernel Version",
		"Digital HW Version",
		"Analogue HW Version",
		"Firmware 1",
		"Firmware 2"};

	int16_t i = 0;
	int16_t requiredSize = 0;
	int8_t line [80];
	int32_t variant;
	PICO_STATUS status = PICO_OK;

	// Variables used for arbitrary waveform parameters
	int16_t			minArbitraryWaveformValue = 0;
	int16_t			maxArbitraryWaveformValue = 0;
	uint32_t		minArbitraryWaveformSize = 0;
	uint32_t		maxArbitraryWaveformSize = 0;

	//Initialise default unit properties and change when required
	unit->sigGen = SIGGEN_FUNCTGEN;
	unit->firstRange = PS5000A_10MV;
	unit->lastRange = PS5000A_20V;
	unit->channelCount = DUAL_SCOPE;
	unit->awgBufferSize = MIN_SIG_GEN_BUFFER_SIZE;
	unit->digitalPortCount = 0;

	if (unit->handle) 
	{
		printf("Device information:-\n\n");

		for (i = 0; i < 11; i++) 
		{
			status = ps5000aGetUnitInfo(unit->handle, line, sizeof (line), &requiredSize, i);

			// info = 3 - PICO_VARIANT_INFO
			if (i == PICO_VARIANT_INFO) 
			{
				variant = atoi(line);
				memcpy(&(unit->modelString), line, sizeof(unit->modelString)==5?5:sizeof(unit->modelString));

				unit->channelCount = (int16_t)line[1];
				unit->channelCount = unit->channelCount - 48; // Subtract ASCII 0 (48)

				// Determine if the device is an MSO
				if (strstr(line, "MSO") != NULL)
				{
					unit->digitalPortCount = 2;
				}
				else
				{
					unit->digitalPortCount = 0;
				}
				
			}
			else if (i == PICO_BATCH_AND_SERIAL)	// info = 4 - PICO_BATCH_AND_SERIAL
			{
				memcpy(&(unit->serial), line, requiredSize);
			}

			printf("%s: %s\n", description[i], line);
		}

		printf("\n");

		// Set sig gen parameters
		// If device has Arbitrary Waveform Generator, find the maximum AWG buffer size
		status = ps5000aSigGenArbitraryMinMaxValues(unit->handle, &minArbitraryWaveformValue, &maxArbitraryWaveformValue, &minArbitraryWaveformSize, &maxArbitraryWaveformSize);
		unit->awgBufferSize = maxArbitraryWaveformSize;

		if (unit->awgBufferSize > 0)
		{
			unit->sigGen = SIGGEN_AWG;
		}
		else
		{
			unit->sigGen = SIGGEN_FUNCTGEN;
		}
	}
}

void setCoupling(UNIT * unit)
{
	PICO_STATUS powerStatus = PICO_OK;
	PICO_STATUS status = PICO_OK;
	PS5000A_DEVICE_RESOLUTION resolution = PS5000A_DR_8BIT;

	int32_t i, ch;
	int32_t count = 0;
	int16_t numValidChannels = unit->channelCount; // Dependent on power setting - i.e. channel A & B if USB powered on 4-channel device
	int16_t numEnabledChannels = 0;
	int16_t retry = FALSE;

	if (unit->channelCount == QUAD_SCOPE)
	{
		powerStatus = ps5000aCurrentPowerSource(unit->handle); 

		if (powerStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
		{
			numValidChannels = DUAL_SCOPE;
		}
	}

	// See what ranges are available... 
	printf("0 -> AC COUPLING\n");
	printf("1 -> DC COUPLING\n");


	do
	{
		count = 0;

		do
		{
			// Ask the user to select a range
			printf("Specify coupling (0 or 1)\n");
			printf("99 - switches channel off\n");
		
			for (ch = 0; ch < numValidChannels; ch++) 
			{
				printf("\n");

				do 
				{
					printf("Channel %c: ", 'A' + ch);
					fflush(stdin);
					scanf_s("%hd", &(unit->channelSettings[ch].DCcoupled));
			
				} while (unit->channelSettings[ch].DCcoupled != 0 && unit->channelSettings[ch].DCcoupled != 1 && unit->channelSettings[ch].DCcoupled != 99);

				if (unit->channelSettings[ch].DCcoupled != 99) 
				{
					if(unit->channelSettings[ch].DCcoupled == 0) printf(" - AC COUPLED\n");
					if(unit->channelSettings[ch].DCcoupled == 1) printf(" - DC COUPLED\n");
					unit->channelSettings[ch].enabled = TRUE;
					count++;
				} 
				else 
				{
					printf("Channel Switched off\n");
					unit->channelSettings[ch].enabled = FALSE;
					unit->channelSettings[ch].DCcoupled = 0;
				}
			}
			printf(count == 0? "\n** At least 1 channel must be enabled **\n\n":"");
		}
		while (count == 0);	// must have at least one channel enabled

		status = ps5000aGetDeviceResolution(unit->handle, &resolution);

		// Verify that the number of enabled channels is valid for the resolution set.

		switch (resolution)
		{
			case PS5000A_DR_15BIT:

				if (count > 2)
				{
					printf("\nError: Only 2 channels may be enabled with 15-bit resolution set.\n");
					printf("Please switch off %d channel(s).\n", numValidChannels - 2);
					retry = TRUE;
				}
				else
				{
					retry = FALSE;
				}
				break;

			case PS5000A_DR_16BIT:

				if (count > 1)
				{
					printf("\nError: Only one channes may be enabled with 16-bit resolution set.\n");
					printf("Please switch off %d channel(s).\n", numValidChannels - 1);
					retry = TRUE;
				}
				else
				{
					retry = FALSE;
				}
				
				break;

			default:

				retry = FALSE;
				break;
		}

		printf("\n");
	}
	while (retry == TRUE);

	setDefaults(unit);	// Put these changes into effect
}

/****************************************************************************
* Select input voltage ranges for channels
****************************************************************************/
void setVoltages(UNIT * unit)
{
	PICO_STATUS powerStatus = PICO_OK;
	PICO_STATUS status = PICO_OK;
	PS5000A_DEVICE_RESOLUTION resolution = PS5000A_DR_8BIT;

	int32_t i, ch;
	int32_t count = 0;
	int16_t numValidChannels = unit->channelCount; // Dependent on power setting - i.e. channel A & B if USB powered on 4-channel device
	int16_t numEnabledChannels = 0;
	int16_t retry = FALSE;

	if (unit->channelCount == QUAD_SCOPE)
	{
		powerStatus = ps5000aCurrentPowerSource(unit->handle); 

		if (powerStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
		{
			numValidChannels = DUAL_SCOPE;
		}
	}

	// See what ranges are available... 
	for (i = unit->firstRange; i <= unit->lastRange; i++) 
	{
		printf("%d -> %d mV\n", i, inputRanges[i]);
	}

	do
	{
		count = 0;

		do
		{
			// Ask the user to select a range
			printf("Specify voltage range (%d..%d)\n", unit->firstRange, unit->lastRange);
			printf("99 - switches channel off\n");
		
			for (ch = 0; ch < numValidChannels; ch++) 
			{
				printf("\n");

				do 
				{
					printf("Channel %c: ", 'A' + ch);
					fflush(stdin);
					scanf_s("%hd", &(unit->channelSettings[ch].range));
			
				} while (unit->channelSettings[ch].range != 99 && (unit->channelSettings[ch].range < unit->firstRange || unit->channelSettings[ch].range > unit->lastRange));

				if (unit->channelSettings[ch].range != 99) 
				{
					printf(" - %d mV\n", inputRanges[unit->channelSettings[ch].range]);
					unit->channelSettings[ch].enabled = TRUE;
					count++;
				} 
				else 
				{
					printf("Channel Switched off\n");
					unit->channelSettings[ch].enabled = FALSE;
					unit->channelSettings[ch].range = PS5000A_MAX_RANGES-1;
				}
			}
			printf(count == 0? "\n** At least 1 channel must be enabled **\n\n":"");
		}
		while (count == 0);	// must have at least one channel enabled

		status = ps5000aGetDeviceResolution(unit->handle, &resolution);

		// Verify that the number of enabled channels is valid for the resolution set.

		switch (resolution)
		{
			case PS5000A_DR_15BIT:

				if (count > 2)
				{
					printf("\nError: Only 2 channels may be enabled with 15-bit resolution set.\n");
					printf("Please switch off %d channel(s).\n", numValidChannels - 2);
					retry = TRUE;
				}
				else
				{
					retry = FALSE;
				}
				break;

			case PS5000A_DR_16BIT:

				if (count > 1)
				{
					printf("\nError: Only one channes may be enabled with 16-bit resolution set.\n");
					printf("Please switch off %d channel(s).\n", numValidChannels - 1);
					retry = TRUE;
				}
				else
				{
					retry = FALSE;
				}
				
				break;

			default:

				retry = FALSE;
				break;
		}

		printf("\n");
	}
	while (retry == TRUE);

	setDefaults(unit);	// Put these changes into effect
}

/****************************************************************************
* setTimebase
* Select timebase, set time units as nano seconds
*
****************************************************************************/
void setTimebase(UNIT * unit)
{
	PICO_STATUS status = PICO_OK;
	PICO_STATUS powerStatus = PICO_OK;
	int32_t timeInterval;
	int32_t maxSamples;
	int32_t ch;

	uint32_t shortestTimebase;
	double timeIntervalSeconds;
	
	PS5000A_CHANNEL_FLAGS enabledChannelOrPortFlags = (PS5000A_CHANNEL_FLAGS)0;
	
	int16_t numValidChannels = unit->channelCount; // Dependent on power setting - i.e. channel A & B if USB powered on 4-channel device
	
	if (unit->channelCount == QUAD_SCOPE)
	{
		powerStatus = ps5000aCurrentPowerSource(unit->handle);
		
		if (powerStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
		{
			numValidChannels = DUAL_SCOPE;
		}
	}
	
	// Find the analogue channels that are enabled - if an MSO model is being used, this will need to be
	// modified to add channel flags for enabled digital ports
	for (ch = 0; ch < numValidChannels; ch++)
	{
		if (unit->channelSettings[ch].enabled)
		{
			enabledChannelOrPortFlags = enabledChannelOrPortFlags | (PS5000A_CHANNEL_FLAGS)(1 << ch);
		}
	}
	
	// Find the shortest possible timebase and inform the user.
	status = ps5000aGetMinimumTimebaseStateless(unit->handle, enabledChannelOrPortFlags, &shortestTimebase, &timeIntervalSeconds, unit->resolution);

	if (status != PICO_OK)
	{
		printf("setTimebase:ps5000aGetMinimumTimebaseStateless ------ 0x%08lx \n", status);
		return;
	}

	printf("Shortest timebase index available %d (%.9f seconds).\n", shortestTimebase, timeIntervalSeconds);
	
	printf("Specify desired timebase: ");
	fflush(stdin);
	scanf_s("%lud", &timebase);

	do 
	{
		status = ps5000aGetTimebase(unit->handle, timebase, BUFFER_SIZE, &timeInterval, &maxSamples, 0);

		if (status == PICO_INVALID_NUMBER_CHANNELS_FOR_RESOLUTION)
		{
			printf("SetTimebase: Error - Invalid number of channels for resolution.\n");
			return;
		}
		else if (status == PICO_OK)
		{
			// Do nothing
		}
		else
		{
			timebase++; // Increase timebase if the one specified can't be used. 
		}

	}
	while (status != PICO_OK);

	printf("Timebase used %lu = %ld ns sample interval\n", timebase, timeInterval);
}

/****************************************************************************
* printResolution
*
* Outputs the resolution in text format to the console window
****************************************************************************/
void printResolution(PS5000A_DEVICE_RESOLUTION * resolution)
{
	switch(*resolution)
	{
		case PS5000A_DR_8BIT:

			printf("8 bits");
			break;

		case PS5000A_DR_12BIT:

			printf("12 bits");
			break;

		case PS5000A_DR_14BIT:

			printf("14 bits");
			break;

		case PS5000A_DR_15BIT:

			printf("15 bits");
			break;

		case PS5000A_DR_16BIT:

			printf("16 bits");
			break;

		default:

			break;
	}

	printf("\n");
}

/****************************************************************************
* setResolution
* Set resolution for the device
*
****************************************************************************/
void setResolution(UNIT * unit)
{
	int16_t value;
	int16_t i;
	int16_t numEnabledChannels = 0;
	int16_t retry;
	int32_t resolutionInput;

	PICO_STATUS status;
	PS5000A_DEVICE_RESOLUTION resolution;
	PS5000A_DEVICE_RESOLUTION newResolution = PS5000A_DR_8BIT;

	// Determine number of channels enabled
	for(i = 0; i < unit->channelCount; i++)
	{
		if(unit->channelSettings[i].enabled == TRUE)
		{
			numEnabledChannels++;
		}
	}

	if(numEnabledChannels == 0)
	{
		printf("setResolution: Please enable channels.\n");
		return;
	}

	status = ps5000aGetDeviceResolution(unit->handle, &resolution);

	if(status == PICO_OK)
	{
		printf("Current resolution: ");
		printResolution(&resolution);
	}
	else
	{
		printf("setResolution:ps5000aGetDeviceResolution ------ 0x%08lx \n", status);
		return;
	}

	printf("\n");

	printf("Select device resolution:\n");
	printf("0: 8 bits\n");
	printf("1: 12 bits\n");
	printf("2: 14 bits\n");

	if(numEnabledChannels <= 2)
	{
		printf("3: 15 bits\n");
	}

	if(numEnabledChannels == 1)
	{
		printf("4: 16 bits\n\n");
	}

	retry = TRUE;

	do
	{
		if(numEnabledChannels == 1)
		{
			printf("Resolution [0...4]: ");
		}
		else if(numEnabledChannels == 2)
		{
			printf("Resolution [0...3]: ");
		}
		else
		{
			printf("Resolution [0...2]: ");
		}
	
		fflush(stdin);
		scanf_s("%lud", &resolutionInput);

		newResolution = (PS5000A_DEVICE_RESOLUTION)resolutionInput;

		// Verify if resolution can be selected for number of channels enabled

		if (newResolution == PS5000A_DR_16BIT && numEnabledChannels > 1)
		{
			printf("setResolution: 16 bit resolution can only be selected with 1 channel enabled.\n");
		}
		else if (newResolution == PS5000A_DR_15BIT && numEnabledChannels > 2)
		{
			printf("setResolution: 15 bit resolution can only be selected with a maximum of 2 channels enabled.\n");
		}
		else if(newResolution < PS5000A_DR_8BIT && newResolution > PS5000A_DR_16BIT)
		{
			printf("setResolution: Resolution index selected out of bounds.\n");
		}
		else
		{
			retry = FALSE;
		}
	}
	while(retry);
	
	printf("\n");

	status = ps5000aSetDeviceResolution(unit->handle, (PS5000A_DEVICE_RESOLUTION) newResolution);

	if (status == PICO_OK)
	{
		unit->resolution = newResolution;

		printf("Resolution selected: ");
		printResolution(&newResolution);
		
		// The maximum ADC value will change if transitioning from 8 bit to >= 12 bit or vice-versa
		ps5000aMaximumValue(unit->handle, &value);
		unit->maxADCValue = value;
	}
	else
	{
		printf("setResolution:ps5000aSetDeviceResolution ------ 0x%08lx \n", status);
	}

}

/****************************************************************************
* collectStreamingTriggered
*  This function demonstrates how to collect a stream of data
*  from the unit (start collecting on trigger)
***************************************************************************/
void collectStreamingTriggered(UNIT * unit)
{
	int16_t triggerVoltage = 500; // mV
	PS5000A_CHANNEL triggerChannel = PS5000A_CHANNEL_A;
	int16_t voltageRange = inputRanges[unit->channelSettings[triggerChannel].range];
	int16_t triggerThreshold = 0;

	// Structures for setting up trigger - declare each as an array of multiple structures if using multiple channels
	struct tPS5000ATriggerChannelPropertiesV2 triggerProperties;
	struct tPS5000ACondition conditions;
	struct tPS5000ADirection directions;

	// Struct to hold Pulse Width Qualifier information
	struct tPwq pulseWidth;

	memset(&triggerProperties, 0, sizeof(struct tPS5000ATriggerChannelPropertiesV2));
	memset(&conditions, 0, sizeof(struct tPS5000ACondition));
	memset(&directions, 0, sizeof(struct tPS5000ADirection));
	memset(&pulseWidth, 0, sizeof(struct tPwq));

	// If the channel is not enabled, warn the User and return
	if (unit->channelSettings[triggerChannel].enabled == 0)
	{
		printf("collectStreamingTriggered: Channel not enabled.");
		return;
	}

	// If the trigger voltage level is greater than the range selected, set the threshold to half
	// of the range selected e.g. for ±200 mV, set the threshold to 100 mV
	if (triggerVoltage > voltageRange)
	{
		triggerVoltage = (voltageRange / 2);
	}

	triggerThreshold = mv_to_adc(triggerVoltage, unit->channelSettings[triggerChannel].range, unit);

	// Set trigger channel properties
	triggerProperties.thresholdUpper = triggerThreshold;
	triggerProperties.thresholdUpperHysteresis = 256 * 10;
	triggerProperties.thresholdLower = triggerThreshold;
	triggerProperties.thresholdLowerHysteresis = 256 * 10;
	triggerProperties.channel = triggerChannel;

	// Set trigger conditions
	conditions.source = triggerChannel;
	conditions.condition = PS5000A_CONDITION_TRUE;

	// Set trigger directions
	directions.source = triggerChannel;
	directions.direction = PS5000A_RISING;
	directions.mode = PS5000A_LEVEL;
		
	printf("Collect streaming triggered...\n");
	printf("Data is written to disk file (stream.txt)\n");
	printf("Press a key to start\n");
	_getch();
	
	setDefaults(unit);

	/* Trigger enabled
	* Rising edge
	* Threshold = 1000 mV */
	setTrigger(unit, &triggerProperties, 1, &conditions, 1, &directions, 1, &pulseWidth, 0, 0);

	streamDataHandler(unit, 0);
}


/****************************************************************************
* displaySettings 
* Displays information about the user configurable settings in this example
* Parameters 
* - unit        pointer to the UNIT structure
*
* Returns       none
***************************************************************************/
void displaySettings(UNIT *unit)
{
	int32_t ch;
	int32_t voltage;
	PICO_STATUS status = PICO_OK;
	PS5000A_DEVICE_RESOLUTION resolution = PS5000A_DR_8BIT;

	printf("\nReadings will be scaled in %s\n", (scaleVoltages)? ("millivolts") : ("ADC counts"));
	printf("\n");

	for (ch = 0; ch < unit->channelCount; ch++)
	{
		if (!(unit->channelSettings[ch].enabled))
		{
			printf("Channel %c Voltage Range = Off\n", 'A' + ch);
		}
		else
		{
			voltage = inputRanges[unit->channelSettings[ch].range];
			printf("Channel %c Voltage Range = ", 'A' + ch);
			
			if (voltage < 1000)
			{
				printf("%dmV\n", voltage);
			}
			else
			{
				printf("%dV\n", voltage / 1000);
			}
		}
	}

	printf("\n");

	status = ps5000aGetDeviceResolution(unit->handle, &resolution);

	printf("Device Resolution: ");
	printResolution(&resolution);

}

/****************************************************************************
* openDevice 
* Parameters 
* - unit        pointer to the UNIT structure, where the handle will be stored
* - serial		pointer to the int8_t array containing serial number
*
* Returns
* - PICO_STATUS to indicate success, or if an error occurred
***************************************************************************/
PICO_STATUS openDevice(UNIT *unit, int8_t *serial)
{
	PICO_STATUS status;
	unit->resolution = PS5000A_DR_8BIT;

	if (serial == NULL)
	{
		status = ps5000aOpenUnit(&unit->handle, NULL, unit->resolution);
	}
	else
	{
		status = ps5000aOpenUnit(&unit->handle, serial, unit->resolution);
	}

	unit->openStatus = (int16_t) status;
	unit->complete = 1;

	return status;
}

/****************************************************************************
* handleDevice
* Parameters
* - unit        pointer to the UNIT structure, where the handle will be stored
*
* Returns
* - PICO_STATUS to indicate success, or if an error occurred
***************************************************************************/
PICO_STATUS handleDevice(UNIT * unit)
{
	int16_t value = 0;
	int32_t i;
	struct tPwq pulseWidth;
	PICO_STATUS status;

	printf("Handle: %d\n", unit->handle);
	
	if (unit->openStatus != PICO_OK)
	{
		printf("Unable to open device\n");
		printf("Error code : 0x%08x\n", (uint32_t) unit->openStatus);
		while(!_kbhit());
		exit(99); // exit program
	}

	printf("Device opened successfully, cycle %d\n\n", ++cycles);
	
	// Setup device info - unless it's set already
	if (unit->model == MODEL_NONE)
	{
		set_info(unit);
	}

	// Turn off any digital ports (MSO models only)
	if (unit->digitalPortCount > 0)
	{
		printf("Turning off digital ports.");

		for (i = 0; i < unit->digitalPortCount; i++)
		{
			status = ps5000aSetDigitalPort(unit->handle, (PS5000A_CHANNEL)(i + PS5000A_DIGITAL_PORT0), 0, 0);
		}
	}
	
	timebase = 1;

	ps5000aMaximumValue(unit->handle, &value);
	unit->maxADCValue = value;

	status = ps5000aCurrentPowerSource(unit->handle);

	for (i = 0; i < unit->channelCount; i++)
	{
		// Do not enable channels C and D if power supply not connected for PicoScope 544XA/B devices
		if(unit->channelCount == QUAD_SCOPE && status == PICO_POWER_SUPPLY_NOT_CONNECTED && i >= DUAL_SCOPE)
		{
			unit->channelSettings[i].enabled = FALSE;
			unit->channelSettings[4].enabled = TRUE;
		}
		else
		{
			unit->channelSettings[i].enabled = TRUE;
			unit->channelSettings[4].enabled = TRUE;
		}

		unit->channelSettings[i].DCcoupled = FALSE;
		unit->channelSettings[i].range = PS5000A_5V;
		unit->channelSettings[i].analogueOffset = 0.0f;
	}

	memset(&pulseWidth, 0, sizeof(struct tPwq));

	setDefaults(unit);

	/* Trigger disabled	*/
	status = ps5000aSetSimpleTrigger(unit->handle, 0, PS5000A_EXTERNAL, 0, PS5000A_RISING, 0, 0);

	return unit->openStatus;
}

/****************************************************************************
* closeDevice 
****************************************************************************/
void closeDevice(UNIT *unit)
{
	ps5000aCloseUnit(unit->handle);
}

/****************************************************************************
* mainMenu
* Controls default functions of the seelected unit
* Parameters
* - unit        pointer to the UNIT structure
*
* Returns       none
***************************************************************************/
void mainMenu(UNIT *unit)
{
	int8_t ch = '.';
	while (ch != 'X')
	{
		displaySettings(unit);

		printf("\n\n");
		printf("Please select operation:\n\n");

		printf("						C - Coupling AC/DC (Default = AC)\n");
		printf("W - Triggered streaming				V - Set voltages\n");
		printf("R - Collect set of rapid captures		I - Set timebase\n");
		printf("						A - ADC counts/mV\n");
		printf("						D - Set resolution\n");

		printf("X - Exit\n");
		printf("Operation:");

		ch = toupper(_getch());

		printf("\n\n");

		switch (ch) 
		{
			case 'W':
				collectStreamingTriggered(unit);
				break;
				
			case 'R':
				collectRapidBlock(unit);
				break;

			case 'V':
				setVoltages(unit);
				break;

			case 'I':
				setTimebase(unit);
				break;

			case 'A':
				scaleVoltages = !scaleVoltages;
				break;

			case 'D':
				setResolution(unit);
				break;

			case 'X':
				break;

			case 'C':
				setCoupling(unit);
				break;

			default:
				printf("Invalid operation\n");
				break;
		}
	}
}


/****************************************************************************
* main
*
***************************************************************************/
int32_t main(void)
{
	int8_t ch;
	uint16_t devCount = 0, listIter = 0,	openIter = 0;
	//device indexer -  64 chars - 64 is maximum number of picoscope devices handled by driver
	int8_t devChars[] =
			"1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#";
	PICO_STATUS status = PICO_OK;
	UNIT allUnits[MAX_PICO_DEVICES];

	printf("PicoScope 5000 Series (ps5000a) Driver Example Program\n");
	printf("\nEnumerating Units...\n");

	do
	{
		status = openDevice(&(allUnits[devCount]), NULL);
		
		if (status == PICO_OK || status == PICO_POWER_SUPPLY_NOT_CONNECTED 
					|| status == PICO_USB3_0_DEVICE_NON_USB3_0_PORT)
		{
			allUnits[devCount++].openStatus = (int16_t) status;
		}

	} while(status != PICO_NOT_FOUND);

	if (devCount == 0)
	{
		printf("Picoscope devices not found\n");
		return 1;
	}

	// if there is only one device, open and handle it here
	if (devCount == 1)
	{
		printf("Found one device, opening...\n\n");
		status = allUnits[0].openStatus;

		if (status == PICO_OK || status == PICO_POWER_SUPPLY_NOT_CONNECTED
					|| status == PICO_USB3_0_DEVICE_NON_USB3_0_PORT)
		{
			if (allUnits[0].openStatus == PICO_POWER_SUPPLY_NOT_CONNECTED || allUnits[0].openStatus == PICO_USB3_0_DEVICE_NON_USB3_0_PORT)
			{
				allUnits[0].openStatus = (int16_t)changePowerSource(allUnits[0].handle, allUnits[0].openStatus, &allUnits[0]);
			}

			set_info(&allUnits[0]);
			status = handleDevice(&allUnits[0]);
		}

		if (status != PICO_OK)
		{
			printf("Picoscope devices open failed, error code 0x%x\n",(uint32_t)status);
			return 1;
		}

		mainMenu(&allUnits[0]);
		closeDevice(&allUnits[0]);
		printf("Exit...\n");
		return 0;
	}
	else
	{
		// More than one unit
		printf("Found %d devices, initializing...\n\n", devCount);

		for (listIter = 0; listIter < devCount; listIter++)
		{
			if (allUnits[listIter].openStatus == PICO_OK || allUnits[listIter].openStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
			{
				set_info(&allUnits[listIter]);
				openIter++;
			}
		}
	}
	
	// None
	if (openIter == 0)
	{
		printf("Picoscope devices init failed\n");
		return 1;
	}
	// Just one - handle it here
	if (openIter == 1)
	{
		for (listIter = 0; listIter < devCount; listIter++)
		{
			if (!(allUnits[listIter].openStatus == PICO_OK || allUnits[listIter].openStatus == PICO_POWER_SUPPLY_NOT_CONNECTED))
			{
				break;
			}
		}
		
		printf("One device opened successfully\n");
		printf("Model\t: %s\nS/N\t: %s\n", allUnits[listIter].modelString, allUnits[listIter].serial);
		status = handleDevice(&allUnits[listIter]);
		
		if (status != PICO_OK)
		{
			printf("Picoscope device open failed, error code 0x%x\n", (uint32_t)status);
			return 1;
		}
		
		mainMenu(&allUnits[listIter]);
		closeDevice(&allUnits[listIter]);
		printf("Exit...\n");
		return 0;
	}
	printf("Found %d devices, pick one to open from the list:\n", devCount);

	for (listIter = 0; listIter < devCount; listIter++)
	{
		printf("%c) Picoscope %7s S/N: %s\n", devChars[listIter],
				allUnits[listIter].modelString, allUnits[listIter].serial);
	}

	printf("ESC) Cancel\n");

	ch = '.';
	
	// If escape
	while (ch != 27)
	{
		ch = _getch();
		
		// If escape
		if (ch == 27)
			continue;
		for (listIter = 0; listIter < devCount; listIter++)
		{
			if (ch == devChars[listIter])
			{
				printf("Option %c) selected, opening Picoscope %7s S/N: %s\n",
						devChars[listIter], allUnits[listIter].modelString,
						allUnits[listIter].serial);
				
				if ((allUnits[listIter].openStatus == PICO_OK || allUnits[listIter].openStatus == PICO_POWER_SUPPLY_NOT_CONNECTED))
				{
					status = handleDevice(&allUnits[listIter]);
				}
				
				if (status != PICO_OK)
				{
					printf("Picoscope devices open failed, error code 0x%x\n", (uint32_t)status);
					return 1;
				}

				mainMenu(&allUnits[listIter]);

				printf("Found %d devices, pick one to open from the list:\n",devCount);
				
				for (listIter = 0; listIter < devCount; listIter++)
				{
					printf("%c) Picoscope %7s S/N: %s\n", devChars[listIter],
							allUnits[listIter].modelString,
							allUnits[listIter].serial);
				}
				
				printf("ESC) Cancel\n");
			}
		}
	}

	for (listIter = 0; listIter < devCount; listIter++)
	{
		closeDevice(&allUnits[listIter]);
	}

	printf("Exit...\n");
	
	return 0;
}
