/*
 * SerialConsole.cpp
 *
 Copyright (c) 2014 Collin Kidder

 Shamelessly copied from the version in GEVCU

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include "SerialConsole.h"
#include <due_wire.h>
#include <Wire_EEPROM.h>
#include <due_can.h>

SerialConsole::SerialConsole() {
	init();
}

void SerialConsole::init() {
	//State variables for serial console
	ptrBuffer = 0;
	state = STATE_ROOT_MENU;      
}

void SerialConsole::printMenu() {
	char buff[80];
	//Show build # here as well in case people are using the native port and don't get to see the start up messages
	SerialUSB.print("Build number: ");
	SerialUSB.println(CFG_BUILD_NUM);
	SerialUSB.println("System Menu:");
	SerialUSB.println();
	SerialUSB.println("Enable line endings of some sort (LF, CR, CRLF)");
	SerialUSB.println();
	SerialUSB.println("Short Commands:");
	SerialUSB.println("h = help (displays this message)");
	SerialUSB.println("K = set all outputs high");
	SerialUSB.println("J = set all outputs low");
	SerialUSB.println("R = reset to factory defaults");
	SerialUSB.println("s = Start logging to file");
	SerialUSB.println("S = Stop logging to file");
	SerialUSB.println();
	SerialUSB.println("Config Commands (enter command=newvalue). Current values shown in parenthesis:");
    SerialUSB.println();

    Logger::console("LOGLEVEL=%i - set log level (0=debug, 1=info, 2=warn, 3=error, 4=off)", settings.logLevel);
	Logger::console("SYSTYPE=%i - set board type (0=CANDue, 1=GEVCU)", settings.sysType);
	SerialUSB.println();

	Logger::console("CAN0EN=%i - Enable/Disable CAN0 (0 = Disable, 1 = Enable)", settings.CAN0_Enabled);
	Logger::console("CAN0SPEED=%i - Set speed of CAN0 in baud (125000, 250000, etc)", settings.CAN0Speed);
	for (int i = 0; i < 8; i++) {
		sprintf(buff, "CAN0FILTER%i=0x%%x,0x%%x,%%i,%%i (ID, Mask, Extended, Enabled)", i);
		Logger::console(buff, settings.CAN0Filters[i].id, settings.CAN0Filters[i].mask,
			settings.CAN0Filters[i].extended, settings.CAN0Filters[i].enabled);
	}
	SerialUSB.println();

	Logger::console("CAN1EN=%i - Enable/Disable CAN1 (0 = Disable, 1 = Enable)", settings.CAN1_Enabled);
	Logger::console("CAN1SPEED=%i - Set speed of CAN1 in baud (125000, 250000, etc)", settings.CAN1Speed);
	for (int i = 0; i < 8; i++) {
		sprintf(buff, "CAN1FILTER%i=0x%%x,0x%%x,%%i,%%i (ID, Mask, Extended, Enabled)", i);
		Logger::console(buff, settings.CAN1Filters[i].id, settings.CAN1Filters[i].mask,
			settings.CAN1Filters[i].extended, settings.CAN1Filters[i].enabled);
	}
	SerialUSB.println();

	Logger::console("BINSERIAL=%i - Enable/Disable Binary Sending of CANBus Frames to Serial (0=Dis, 1=En)", settings.useBinarySerialComm);
	Logger::console("BINFILE=%i - Enable/Disable Binary File Format (0=Ascii, 1=Binary)", settings.useBinaryFile);
	SerialUSB.println();

	Logger::console("FILEBASE=%s - Set filename base for saving", (char *)settings.fileNameBase);
	Logger::console("FILEEXT=%s - Set filename ext for saving", (char *)settings.fileNameExt);
	Logger::console("FILENUM=%i - Set incrementing number for filename", settings.fileNum);
	Logger::console("FILEAPPEND=%i - Append to file (no numbers) or use incrementing numbers after basename (0=Incrementing Numbers, 1=Append)", settings.appendFile);
}

/*	There is a help menu (press H or h or ?)
 This is no longer going to be a simple single character console.
 Now the system can handle up to 80 input characters. Commands are submitted
 by sending line ending (LF, CR, or both)
 */
void SerialConsole::rcvCharacter(uint8_t chr) {
	if (chr == 10 || chr == 13) { //command done. Parse it.
		handleConsoleCmd();
		ptrBuffer = 0; //reset line counter once the line has been processed
	} else {
		cmdBuffer[ptrBuffer++] = (unsigned char) chr;
		if (ptrBuffer > 79)
			ptrBuffer = 79;
	}
}

void SerialConsole::handleConsoleCmd() {
	if (state == STATE_ROOT_MENU) {
		if (ptrBuffer == 1) { //command is a single ascii character
			handleShortCmd();
		} else { //if cmd over 1 char then assume (for now) that it is a config line
			handleConfigCmd();
		}
		ptrBuffer = 0; //reset line counter once the line has been processed
	}
}

/*For simplicity the configuration setting code uses four characters for each configuration choice. This makes things easier for
 comparison purposes.
 */
void SerialConsole::handleConfigCmd() {
	int i;
	int newValue;
	char *newString;
	bool writeEEPROM = false;

	//Logger::debug("Cmd size: %i", ptrBuffer);
	if (ptrBuffer < 6)
		return; //4 digit command, =, value is at least 6 characters
	cmdBuffer[ptrBuffer] = 0; //make sure to null terminate
	String cmdString = String();
	unsigned char whichEntry = '0';
	i = 0;

	while (cmdBuffer[i] != '=' && i < ptrBuffer) {
	 cmdString.concat(String(cmdBuffer[i++]));
	}
	i++; //skip the =
	if (i >= ptrBuffer)
	{
		Logger::console("Command needs a value..ie TORQ=3000");
		Logger::console("");
		return; //or, we could use this to display the parameter instead of setting
	}

	// strtol() is able to parse also hex values (e.g. a string "0xCAFE"), useful for enable/disable by device id
	newValue = strtol((char *) (cmdBuffer + i), NULL, 0); //try to turn the string into a number
	newString = (char *)(cmdBuffer + i); //leave it as a string

	cmdString.toUpperCase();

	if (cmdString == String("CAN0EN")) {
		if (newValue < 0) newValue = 0;
		if (newValue > 1) newValue = 1;
		Logger::console("Setting CAN0 Enabled to %i", newValue);
		settings.CAN0_Enabled = newValue;
		if (newValue == 1) Can0.begin(settings.CAN0Speed, SysSettings.CAN0EnablePin);
		else Can0.disable();
		writeEEPROM = true;
	} else if (cmdString == String("CAN1EN")) {
		if (newValue < 0) newValue = 0;
		if (newValue > 1) newValue = 1;
		Logger::console("Setting CAN1 Enabled to %i", newValue);
		if (newValue == 1) Can1.begin(settings.CAN1Speed, SysSettings.CAN1EnablePin);
		else Can1.disable();
		settings.CAN1_Enabled = newValue;
		writeEEPROM = true;
	} else if (cmdString == String("CAN0SPEED")) {
		if (newValue > 0 && newValue <= 1000000) 
		{
			Logger::console("Setting CAN0 Baud Rate to %i", newValue);
			settings.CAN0Speed = newValue;
			Can0.begin(settings.CAN0Speed, SysSettings.CAN0EnablePin);
			writeEEPROM = true;
		}
		else Logger::console("Invalid baud rate! Enter a value 1 - 1000000");
	} else if (cmdString == String("CAN1SPEED")) {
		if (newValue > 0 && newValue <= 1000000)
		{
			Logger::console("Setting CAN1 Baud Rate to %i", newValue);
			settings.CAN1Speed = newValue;
			Can1.begin(settings.CAN1Speed, SysSettings.CAN1EnablePin);
			writeEEPROM = true;
		}
		else Logger::console("Invalid baud rate! Enter a value 1 - 1000000");
	} else if (cmdString == String("CAN0FILTER0")) { //someone should kick me in the face for this laziness... FIX THIS!
		handleFilterSet(0, 0, newString);
	}
	else if (cmdString == String("CAN0FILTER1")) {
		if (handleFilterSet(0, 1, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN0FILTER2")) {
		if (handleFilterSet(0, 2, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN0FILTER3")) {
		if (handleFilterSet(0, 3, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN0FILTER4")) {
		if (handleFilterSet(0, 4, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN0FILTER5")) {
		if (handleFilterSet(0, 5, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN0FILTER6")) {
		if (handleFilterSet(0, 6, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN0FILTER7")) {
		if (handleFilterSet(0, 7, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN1FILTER0")) {
		if (handleFilterSet(1, 0, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN1FILTER1")) {
		if (handleFilterSet(1, 1, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN1FILTER2")) {
		if (handleFilterSet(1, 2, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN1FILTER3")) {
		if (handleFilterSet(1, 3, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN1FILTER4")) {
		if (handleFilterSet(1, 4, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN1FILTER5")) {
		if (handleFilterSet(1, 5, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN1FILTER6")) {
		if (handleFilterSet(1, 6, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("CAN1FILTER7")) {
		if (handleFilterSet(1, 7, newString)) writeEEPROM = true;
	}
	else if (cmdString == String("BINSERIAL")) {
		if (newValue < 0) newValue = 0;
		if (newValue > 1) newValue = 1;
		Logger::console("Setting Serial Binary Comm to %i", newValue);
		settings.useBinarySerialComm = newValue;
		writeEEPROM = true;
	} else if (cmdString == String("BINFILE")) {
		if (newValue < 0) newValue = 0;
		if (newValue > 1) newValue = 1;
		Logger::console("Setting File Binary Writing to %i", newValue);
		settings.useBinaryFile = newValue;
		writeEEPROM = true;
	} else if (cmdString == String("FILEBASE")) {
		Logger::console("Setting File Base Name to %s", newString);
		strcpy((char *)settings.fileNameBase, newString);
		writeEEPROM = true;
	} else if (cmdString == String("FILEEXT")) {
		Logger::console("Setting File Extension to %s", newString);
		strcpy((char *)settings.fileNameExt, newString);
		writeEEPROM = true;
	} else if (cmdString == String("FILENUM")) {
		Logger::console("Setting File Incrementing Number Base to %i", newValue);
		settings.fileNum = newValue;
		writeEEPROM = true;
	} else if (cmdString == String("FILEAPPEND")) {
		if (newValue < 0) newValue = 0;
		if (newValue > 1) newValue = 1;
		Logger::console("Setting File Append Mode to %i", newValue);
		settings.appendFile = newValue;
		writeEEPROM = true;
	}
	else if (cmdString == String("SYSTYPE")) {
		if (newValue < 2 && newValue >= 0) {
			settings.sysType = newValue;			
			writeEEPROM = true;
			Logger::console("System type updated. Power cycle to apply.");
		}
		else Logger::console("Invalid system type. Please enter a value of 0 for CanDue or 1 for GEVCU");       
	} else if (cmdString == String("LOGLEVEL")) {
		switch (newValue) {
		case 0:
			Logger::setLoglevel(Logger::Debug);
			Logger::console("setting loglevel to 'debug'");
			writeEEPROM = true;
			break;
		case 1:
			Logger::setLoglevel(Logger::Info);
			Logger::console("setting loglevel to 'info'");
			writeEEPROM = true;
			break;
		case 2:
			Logger::console("setting loglevel to 'warning'");
			Logger::setLoglevel(Logger::Warn);
			writeEEPROM = true;
			break;
		case 3:
			Logger::console("setting loglevel to 'error'");
			Logger::setLoglevel(Logger::Error);
			writeEEPROM = true;
			break;
		case 4:
			Logger::console("setting loglevel to 'off'");
			Logger::setLoglevel(Logger::Off);
			writeEEPROM = true;
			break;
		}

	} 
	else {
		Logger::console("Unknown command");
	}
	if (writeEEPROM) 
	{
		EEPROM.write(EEPROM_PAGE, settings);
	}
}

void SerialConsole::handleShortCmd() {
	uint8_t val;

	switch (cmdBuffer[0]) {
	case 'h':
	case '?':
	case 'H':
		printMenu();
		break;
	case 'K': //set all outputs high
		for (int tout = 0; tout < NUM_OUTPUT; tout++) setOutput(tout, true);
		Logger::console("all outputs: ON");
		break;
	case 'J': //set the four outputs low
		for (int tout = 0; tout < NUM_OUTPUT; tout++) setOutput(tout, false);
		Logger::console("all outputs: OFF");
		break;        
	case 'R': //reset to factory defaults.
		break;
	case 's': //start logging canbus to file
		break;
	case 'S': //stop logging canbus to file
		break;
	case 'X':
		setup(); //this is probably a bad idea. Do not do this while connected to anything you care about - only for debugging in safety!
		break;
	}
}

//CAN0FILTER%i=%%i,%%i,%%i,%%i (ID, Mask, Extended, Enabled)", i);
bool SerialConsole::handleFilterSet(uint8_t bus, uint8_t filter, char *values) 
{
	if (filter < 0 || filter > 7) return false;
	if (bus < 0 || bus > 1) return false;

	//there should be four tokens
	char *idTok = strtok(values, ",");
	char *maskTok = strtok(NULL, ",");
	char *extTok = strtok(NULL, ",");
	char *enTok = strtok(NULL, ",");

	if (!idTok) return false; //if any of them were null then something was wrong. Abort.
	if (!maskTok) return false;
	if (!extTok) return false;
	if (!enTok) return false;

	int idVal = strtol(idTok, NULL, 0);
	int maskVal = strtol(maskTok, NULL, 0);
	int extVal = strtol(extTok, NULL, 0);
	int enVal = strtol(enTok, NULL, 0);

	Logger::console("Setting CAN%iFILTER%i to ID 0x%x Mask 0x%x Extended %i Enabled %i", bus, filter, idVal, maskVal, extVal, enVal);

	if (bus == 0)
	{
		settings.CAN0Filters[filter].id = idVal;
		settings.CAN0Filters[filter].mask = maskVal;
		settings.CAN0Filters[filter].extended = extVal;
		settings.CAN0Filters[filter].enabled = enVal;
		Can0.setRXFilter(filter, idVal, maskVal, extVal);
	}
	else if (bus == 1) 
	{
		settings.CAN1Filters[filter].id = idVal;
		settings.CAN1Filters[filter].mask = maskVal;
		settings.CAN1Filters[filter].extended = extVal;
		settings.CAN1Filters[filter].enabled = enVal;
		Can1.setRXFilter(filter, idVal, maskVal, extVal);
	}

	return true;
}