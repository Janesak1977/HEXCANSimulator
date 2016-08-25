//****************************
// HEXCAN Interface Simulator
//****************************

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include "HEXCANSimulator.h"


#define RXTimeout	50
#define TXTimeout	100
#define COMPortName	"COM1"

DCB dcb;
HANDLE hSerialPort;
HANDLE hWriteEvent;
HANDLE hReadEvent;
DWORD EventMask;

unsigned char volatile ThreadEnabled = 0;
unsigned char volatile PacketReceived = 0;
OVERLAPPED ovWrite;
OVERLAPPED ovRead;
DWORD dwBytesWritten = 0;
DWORD dwBytesRead = 0;
unsigned char PacketDataLen;
unsigned char CANCfgMode;
unsigned char PacketCMD;
unsigned char CANRXBuffer[16] = {0x00};
unsigned char szRxChar;
unsigned char RXBuffPtr;

unsigned char volatile RXBuffer[128];
unsigned char TXBuffer[128];

union {
    char  myByte[4];
    _int32 mylong;
} tmpBaud;

int WaitForPacket(void);


DWORD WINAPI SerialThreadProc(LPVOID lpParameter) 
{
	unsigned char szRxChar = 0;
	unsigned int RXBuffPtr = 0;
	unsigned char DataLen = 0;
	HANDLE hReadEvent;
	

	printf("[thread]:Worked thread started.\n\r");
	printf("[thread]:Waiting for Packets...\n\r\n\r");

	hReadEvent = CreateEvent(NULL, false, false,"RxEvent");
	ovRead.hEvent = hReadEvent;

	while(ThreadEnabled)
	{
		do
		{
			if (HasOverlappedIoCompleted(&ovRead))
			{
				if (!ReadFile(hSerialPort, &szRxChar, 1, &dwBytesRead, &ovRead))
				{
					DWORD dwErr = GetLastError();
					if (dwErr!=ERROR_IO_PENDING)
						return dwErr;
				}
			}
			if (ThreadEnabled==0)
			{
				printf("[thread]:Worker thread is quiting.\n\r");
				CloseHandle(hReadEvent);
				return(0);
			}
		}while(szRxChar != 0x53);

		RXBuffer[RXBuffPtr++] = szRxChar;
		printf("[thread]: %X",szRxChar);

		
		//Waiting for Packet Length byte
		if (HasOverlappedIoCompleted(&ovRead))
		{
			if (!ReadFile(hSerialPort, &szRxChar, 1, &dwBytesRead, &ovRead))
			{
				DWORD dwErr = GetLastError();
				if (dwErr!=ERROR_IO_PENDING)
					return dwErr;
			}
		}
		
		RXBuffer[RXBuffPtr++] = szRxChar;
		unsigned char DataLen = szRxChar - 3;
		printf(", %X",szRxChar);


		//Waiting for Command byte
		if (HasOverlappedIoCompleted(&ovRead))
		{
			if (!ReadFile(hSerialPort, &szRxChar, 1, &dwBytesRead, &ovRead))
			{
				DWORD dwErr = GetLastError();
				if (dwErr!=ERROR_IO_PENDING)
					return dwErr;
			}
		}

		RXBuffer[RXBuffPtr++] = szRxChar;
		printf(", %X",szRxChar);

		unsigned char i;
		//Read next (PacketLength-3) bytes 
		for(i=1; i<=DataLen ;i++)
		{
			if (HasOverlappedIoCompleted(&ovRead))
			{
				if (!ReadFile(hSerialPort, &szRxChar, 1, &dwBytesRead, &ovRead))
				{
					DWORD dwErr = GetLastError();
					if (dwErr!=ERROR_IO_PENDING)
						return dwErr;
				}
			}
			RXBuffer[RXBuffPtr++] = szRxChar;
			printf(", %X", szRxChar);
		}

		printf("\n\r");
		PacketReceived = 1;
		RXBuffPtr = 0;
		szRxChar = 0x00;
	}
	
	printf("[thread]:Worker thread is quiting.\n\r");
    CloseHandle(hReadEvent);
	return(0);
}



void SendACKPacket(unsigned char *data, unsigned char len, unsigned char ReplyCMD)
{
	unsigned char chksum;
	DWORD BytesWritten;

	TXBuffer[0] = 0x4D;			//Reply MAGIC
	chksum = 0x4D;
	TXBuffer[1] = len + 4;
	chksum ^= len + 4;
	TXBuffer[2] = ReplyCMD;
	chksum ^= ReplyCMD;

	unsigned char* Dest = TXBuffer + 3;
	unsigned char* Src = data;
	while(len!=0)
	{
		chksum = chksum ^ *Src;
		*Dest++ = *Src++;
		len--;
	};

	*Dest = chksum;
	unsigned char SendBufLen = (unsigned char) ((Dest - TXBuffer) + 1);

	WriteFile(hSerialPort, TXBuffer, SendBufLen, &BytesWritten, &ovWrite);

	printf("[SendACKPacket]:Sending reply packet.\n\r", ReplyCMD );
}

void SendPacket(unsigned char* pChecksum)
{
	DWORD BytesWritten;
	unsigned char chksum = 0;
	unsigned char* Ptr = TXBuffer;
	unsigned char PacketLength = (unsigned char)((pChecksum - TXBuffer) + 1);

	TXBuffer[1] = PacketLength;
	do
	{
		chksum = chksum ^ *(Ptr);
		Ptr++;
	}while(Ptr!=pChecksum);

	*(pChecksum) = chksum;

	WriteFile(hSerialPort, TXBuffer, PacketLength, &BytesWritten, &ovWrite);
	printf("[SendPacket]:Sending reply packet.\n\r" );
}



unsigned char* CreatePacketHeader(unsigned char ReplyCMD)
{	
	TXBuffer[0] = TXMagicByte;
	TXBuffer[1] = 0x03;
	TXBuffer[2] = ReplyCMD; 

	return &TXBuffer[3];
}


void GenKEYfromSEED(struct sDLONG* SEED,struct sQLONG* pQLONG)
{
	unsigned __int32 constant = 0;
	unsigned char i=32;

	while (i!=0)
	{
		constant += 0x9E3779B9;
		SEED->First += (((SEED->Second * 16) + pQLONG->First) ^ (SEED->Second + constant)) ^ ((SEED->Second / 32) + pQLONG->Second);
		SEED->Second += (((SEED->First * 16) + pQLONG->Third) ^ (SEED->First + constant)) ^ ((SEED->First / 32) + pQLONG->Fourth);
		i--;
	}
}

void DoCANMode()
{
	unsigned char volatile Ptr;
	CANCfgMode = 1;

	do
	{
		if (CANCfgMode==0)
		{
			

		}

		if (PacketReceived==1);
		{
			sRXPacket_t* pRXPacket = (sRXPacket_t*)RXBuffer;
			PacketDataLen = (pRXPacket->Length) - 4;
			PacketCMD = pRXPacket->Command;

			PacketReceived = 0;

			switch(PacketCMD)
			{
				case CANMode_SendMsg:		// CMD:0xB8 - send CAN message
					Ptr = 0;
					//printf("[main]:Received SendCANMsg Command - 0xB8, CANMSG: ");

					while(Ptr < PacketDataLen)
					{
						CANRXBuffer[Ptr] ^= CANMsgXORTable[Ptr];
						//printf("%X ", CANRXBuffer[Ptr]);
						Ptr++;
					}
					//printf("\n\r");
					SendACKPacket(NULL, 0, ACK_OK);
				break;

				case CANMode_ConfigCAN:			// CMD: 0xB1 - switch to CONFIG mode
					CANCfgMode = 1;
					//printf("[main]:Received EnableCANCfgMode Command - 0xB1\n\r");
					SendACKPacket(NULL, 0, ACK_OK);
				break;

				case CANMode_WriteConfig:		// CMD: 0xB2 - write CAN config register
					//printf("[main]:Received WriteCANConfigReg Command - 0xB2\n\r");
					SendACKPacket(NULL, 0, ACK_OK);
				break;

				case CANMode_WriteMASK:			// CMD: 0xB3 - write CAN mask register
					//printf("[main]:Received WriteCANMaskReg Command - 0xB3\n\r");
					SendACKPacket(NULL, 0,ACK_OK);
				break;

				case CANMode_WriteFILTER:			// CMD: 0xB4 - write CAN filter register
					//printf("[main]:Received WriteCANFilterReg Command - 0xB4\n\r");
					SendACKPacket(NULL, 0,ACK_OK);
				break;

				case CANMode_WriteRXCTRL:			// CMD: 0xB5 - write CAN RX Buffer Control register
					//printf("[main]:Received WriteCANRXBuffCtrlReg Command - 0xB5\n\r");
					SendACKPacket(NULL, 0,ACK_OK);
				break;

				case CANMode_NormalMode:			// CMD:0xB6 - switch to NORMAL mode (from CONFIG mode)
					//printf("[main]:Received EnableCANNormalMode Command - 0xB6\n\r");
					CANCfgMode = 0;
					SendACKPacket(NULL, 0, ACK_OK);
				break;

				case CANMode_StopCANMode:			// CMD: 0xA0 - Stop CANMode and return
					//printf("[main]:Received StopCANMode Command - 0xA0\n\r");
					SendACKPacket(NULL, 0, ACK_OK);
					return;
			}
		}
	}while(1);
}


//********************** MAIN program start ******************************
//************************************************************************
int main(int argc, CHAR* argv[])
{
	int error=0;
	unsigned char i;
	unsigned char DoLoop = 1;

	DWORD dwThreadId;
	HANDLE hThread;
	unsigned char *TXPacktDataPtr;
	unsigned char SubCMD;
	unsigned __int32 KEY = 0x00;
	unsigned char *SEEDPtr;
	unsigned char PacketLen;
	BOOL fSuccess;

	memset(&dcb,0,sizeof(dcb));
	memset(&ovWrite,0,sizeof(ovWrite));
	memset(&ovRead,0,sizeof(ovRead));
	memset(&TXBuffer, 0, sizeof(TXBuffer));
	memset((void*)&RXBuffer, 0, sizeof(RXBuffer));

	hSerialPort = CreateFile( COMPortName,
		GENERIC_READ | GENERIC_WRITE,
		0,    // exclusive access 
		NULL, // default security attributes 
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING,
		NULL 
		);

	if (hSerialPort == INVALID_HANDLE_VALUE) 
	{
		printf ("[main]:Error opening COM port.\n\r");
		return (1);
	}

	dcb.BaudRate = CBR_19200;     // set the baud rate
	dcb.ByteSize = 8;             // data size, xmit, and rcv
	dcb.Parity = NOPARITY;        // no parity bit
	dcb.StopBits = ONESTOPBIT;    // one stop bit

	fSuccess = SetCommState(hSerialPort, &dcb);
	if (!fSuccess) 
	{
		// Handle the error.
		printf ("[main]Error setting baudrate.\n\r");
		return (3);
	}

	PurgeComm(hSerialPort,PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
	
	ThreadEnabled = 1;		//Enable thread loop

	printf("[main]:Strating worker thread.\n\r");
	// Create worker thread for receiving data from FTDI device
	hThread = CreateThread(NULL, 0, SerialThreadProc, NULL ,0, &dwThreadId);
	if (hThread == NULL) 
    {
		printf("[main]Error creating worker thread!");
		return(-1);
	}

	while(DoLoop)
	{
		if (PacketReceived==1);
		{
			sRXPacket_t* pRXPacket = (sRXPacket_t*)RXBuffer;
			PacketLen = pRXPacket->Length;
			PacketCMD = pRXPacket->Command;
			PacketReceived = 0;
			if ((PacketCMD==0xA0) && (PacketLen==4))				
				break;
		}
		if(_kbhit())
		{
			if(_getch()=='q')
			{
				DoLoop = 0;
				ThreadEnabled = 0;
			}
		}
	}
	if (DoLoop==1)
	{
		printf("[main]:Received Init Command - 0xA0\n\r");
		SendACKPacket(NULL, 0, ACK_OK);
	}

	//Main loop
	while(DoLoop)
	{	
		if (PacketReceived==1);
		{
			sRXPacket_t* pRXPacket = (sRXPacket_t*)RXBuffer;
			PacketDataLen = (pRXPacket->Length) - 4;
			
			switch (PacketCMD = pRXPacket->Command)
			{
				case SoftReset:
					PacketReceived = 0;
					SendACKPacket(NULL, 0, ACK_OK);
					//printf("[main]:Received Reset Command - 0x08\n\r");
					PacketReceived = 0;
					break;

				case SetBaudrate:			
					//printf("[main]:Received SetBaudrate Command - 0x03, baudrate: %d \n\r",tmpBaud.mylong);
					tmpBaud.myByte[0] = *(&pRXPacket->Data[0]);
					tmpBaud.myByte[1] = *(&pRXPacket->Data[1]);
					tmpBaud.myByte[2] = *(&pRXPacket->Data[2]);
					tmpBaud.myByte[3] = *(&pRXPacket->Data[3]);	
					SendACKPacket(NULL, 0, ACK_OK);
					Sleep(50);
					dcb.BaudRate = tmpBaud.mylong;
					fSuccess = SetCommState(hSerialPort, &dcb);
					if (!fSuccess) 
					{
						printf ("Error setting baudrate to %d.\n\r", GetLastError());
						return (3);
					}	
					//printf("[main]:Changed baudrate to %d.\n\r", tmpBaud.mylong);
					Sleep(50);
					SendACKPacket(NULL, 0, 0xFD);
					PacketReceived = 0;
					if (PacketReceived==0)
						Sleep(1);
					//printf("[main]:Received ACK to SetBaudrate CMD.\n\r", tmpBaud.mylong);
					PacketReceived = 0;
					break;

				case GetVersion:
					//printf("[main]:Received GetVersion Command - 0x02\n\r",tmpBaud.mylong);
					TXPacktDataPtr = CreatePacketHeader(GetVersion);	//Create Packet header a return pointer to 1.packet data byte
					*(TXPacktDataPtr++) = VERSIONMAJOR;
					*(TXPacktDataPtr++) = VERSIONMINOR;
					*(TXPacktDataPtr++) = 0x44;
					SendPacket(TXPacktDataPtr);			//TXPacktDataPtr points now to Packet checksum
					PacketReceived = 0;
					break;

				case GetInterfaceID:
					//printf("[main]:Received GetInterfaceID Command - 0x04\n\r");
					TXPacktDataPtr = CreatePacketHeader(GetInterfaceID);
					for(i = 0; i<16; i++)
						*(TXPacktDataPtr++) = InterfaceID[i];
					SendPacket(TXPacktDataPtr);			//TXPacktDataPtr points now to Packet checksum
					PacketReceived = 0;
					break;

				case 0x09:
					//printf("[main]:Received Command - 0x09\n\r");
					SubCMD = pRXPacket->Data[0];
					switch(SubCMD)
					{
						case 0x01:														//Generate KEY from SEED
							SEEDPtr = (unsigned char *) &pRXPacket->Data+1;		//Get SEED from packet data
							GenKEYfromSEED( (struct sDLONG*) SEEDPtr, (struct sQLONG*) QLONG00B8 );
							KEY = (unsigned __int32)(*SEEDPtr);
							SendACKPacket((unsigned char*)SEEDPtr, 8, 0x09);
						break;
					}
					PacketReceived = 0;
					break;

				case 0x0D:
					//printf("[main]:Received Command - 0x0D\n\r");
					SendACKPacket(EE001B, 1, 0x0D);
					PacketReceived = 0;
					break;

				case 0x0E:
					//printf("[main]:Received Command - 0x0D\n\r");
					*EE001B = pRXPacket->Data[0];
					SendACKPacket(NULL, 0, ACK_OK);
					PacketReceived = 0;
					break;

				case 0x16:					//CMD: 0x16 - get EEPROM location 0x001C
					//printf("[main]:Received Command - 0x16\n\r");
					SendACKPacket(EE001C, 4, 0x0D);
					PacketReceived = 0;
				break;
			

				case 0x82:
					//printf("[main]:Received KLineTest Command - 0x82\n\r");
					TXPacktDataPtr = CreatePacketHeader(KLineTest);
					*(TXPacktDataPtr++) = 0x00;		//K1Line state
					*(TXPacktDataPtr++) = 0x00;		//K2Line state
					SendPacket(TXPacktDataPtr);
					PacketReceived = 0;
				break;

				case CANMode:
					//printf("[main]:Received CANMode Command - 0xB0\n\r");
					SendACKPacket(NULL, 0, ACK_OK);
					DoCANMode();
					//printf("[main]:Return from CANMode Command\n\r");
					PacketReceived = 0;
				break;

				case 0xA0:
					//printf("[main]:Received Command - 0xA0\n\r");
					SendACKPacket(NULL, 0, ACK_OK);
					PacketReceived = 0;
				break;
			}	
		}
		if(_kbhit())
		{
			if(_getch()=='q')
			{
				DoLoop = 0;
				ThreadEnabled = 0;
			}
		}
	}

	printf("[main]:Waiting for thread exit.\n\r");
	WaitForSingleObject(hThread, INFINITE);

	printf("[main]:Closing all opened handles\n\r");
	CloseHandle(hThread);
	CloseHandle(hSerialPort);
	system("pause");
    return 0;
}

 