#pragma once

#include <cstdint>

namespace mc68k
{
	static constexpr uint32_t g_peripheralMask	= 0xfffff;

	static constexpr uint32_t g_gptBase			= 0xff900;
	static constexpr uint32_t g_simBase			= 0xffa00;
	static constexpr uint32_t g_qsmBase			= 0xffc00;

	static constexpr uint32_t g_gptSize			= 64;
	static constexpr uint32_t g_simSize			= 128;
	static constexpr uint32_t g_qsmSize			= 512;

	enum class PeriphAddress
	{
		// HDI08, note that these are dummy addresses that will be remapped

		HdiICR			= 0x00000,	// Interface Control Register (ICR)
		HdiCVR			= 0x00001,	// Command Vector Register (CVR)
		HdiISR			= 0x00002,	// Interface Status Register (ISR)
		HdiIVR			= 0x00003,	// Interrupt Vector Register (IVR)
		HdiUnused4		= 0x00004,
		HdiTXH			= 0x00005,	// Receive Byte Registers (RXH:RXM:RXL)
		HdiTXM			= 0x00006,	//   or Transmit Byte Registers (TXH:TXM:TXL)
		HdiTXL			= 0x00007,	//   byte order depends on HLEND endianess setting
		HdiFirst		= HdiICR,

		// GPT
		Gptmcr			= 0xFF900,	// GPT Module Configuration Register $YFF900
		Icr				= 0xFF904,	// GPT Interrupt Configuration Register $YFF904
		DdrGp			= 0xFF906,	// Port GP Data Direction Register $YFF906
		PortGp			= 0xFF907,	// Port GP Data Register $YFF907
		Oc1m			= 0xFF908,  // OC1 Action Mask Register $YFF908
		Oc1d			= 0xFF909,  // OC1 Action Dta Register $YFF909
		Tcnt			= 0xFF90a,	// Timer Counter
		TcntLSB			= 0xFF90b,
		Toc1			= 0xFF914,	// TOC[1:4] - Output Compare Registers 1-4 $YFF914 - $YFF91A
		Toc2			= 0xFF916,
		Toc3			= 0xFF918,
		Toc4			= 0xFF91a,
		Tctl1			= 0xFF91e,	// TCTL1/TCTL2 - Timer Control Registers 1 and 2 $YFF91E
		Tmsk1			= 0xFF920,	// TMSK1/TMSK2 - Timer Interrupt Mask Registers 1 and 2 $YFF920
		Tflg1			= 0xFF922,	// TFLG1/TFLG2 - Timer Interrupt Flag Registers 1 and 2 $YFF922
		Cforc			= 0xFF924,	// CFORC - Compare Force Register
		PwmC			= 0xFF925,	// PWMC - PWM Control Register C
		PwmA			= 0xFF926,	// PWMA - PWM Register A
		PwmB			= 0xFF927,	// PWMB - PWM Register B
		PwmCnt			= 0xFF928,	// PWMCNT - PWM Count Register
		PwmBufA			= 0xFF92a,	// PWMBUFA - PWM Buffer Register A
		PwmBufB			= 0xFF92b,	// PWMBUFB - PWM Buffer Register B
		Prescl			= 0xFF92c,	// PRESCL - GPT Prescaler

		// SIM
		Syncr			= 0xFFA04,	// $YFFA04 CLOCK SYNTHESIZER CONTROL (SYNCR)
		PortE0			= 0xFFA11,	// $YFFA11 Port E Data Register
		PortE1			= 0xFFA13,	// $YFFA13 Port E Data Register
		DdrE			= 0xFFA15,	// $YFFA15 Port E Direction Register
		PEPar			= 0xFFA17,	// $YFFA17 Port E Pin Assignment Register
		PortF0			= 0xFFA19,	// $YFFA19 Port F Data Register
		PortF1			= 0xFFA1B,	// $YFFA1B Port F Data Register
		DdrF			= 0xFFA1D,	// $YFFA1D Port F Direction Register
		PFPar			= 0xFFA1F,	// $YFFA1F Port F Pin Assignment Register
		Picr			= 0xFFA22,	// $YFFA22 Periodic Interrupt Control Register
		Pitr			= 0xFFA24,	// $YFFA24 Periodic Interrupt Timer Register

		Cspar0			= 0xFFA44,	// $YFFA44 Chip Select Pin Assignment Register 0
		Cspar1			= 0xFFA46,	// $YFFA46 Chip Select Pin Assignment Register 1
		Csbarbt			= 0xFFA48,	// $YFFA48 Chip Select Base Address Register Boot ROM
		Csorbt			= 0xFFA4A,	// $YFFA4A Chip Select Option Register Boot ROM

		Csbar0			= 0xFFA4C,	// $YFFA4C Chip Select Base Address Register 0
		Csor0			= 0xFFA4E,	// $YFFA4E Chip Select Option Register 0
		Csbar1			= 0xFFA50,	// $YFFA50 Chip Select Base Address Register 1
		Csor1			= 0xFFA52,	// $YFFA52 Chip Select Option Register 1
		Csbar2			= 0xFFA54,	// $YFFA54 Chip Select Base Address Register 2
		Csor2			= 0xFFA56,	// $YFFA56 Chip Select Option Register 2
		Csbar3			= 0xFFA58,	// $YFFA58 Chip Select Base Address Register 3
		Csor3			= 0xFFA5A,	// $YFFA5A Chip Select Option Register 3
		Csbar4			= 0xFFA5C,	// $YFFA5C Chip Select Base Address Register 4
		Csor4			= 0xFFA5E,	// $YFFA5E Chip Select Option Register 4
		Csbar5			= 0xFFA60,	// $YFFA60 Chip Select Base Address Register 5
		Csor5			= 0xFFA62,	// $YFFA62 Chip Select Option Register 5
		Csbar6			= 0xFFA64,	// $YFFA64 Chip Select Base Address Register 6
		Csor6			= 0xFFA66,	// $YFFA66 Chip Select Option Register 6
		Csbar7			= 0xFFA68,	// $YFFA68 Chip Select Base Address Register 7
		Csor7			= 0xFFA6A,	// $YFFA6A Chip Select Option Register 7
		Csbar8			= 0xFFA6C,	// $YFFA6C Chip Select Base Address Register 8
		Csor8			= 0xFFA6E,	// $YFFA6E Chip Select Option Register 8
		Csbar9			= 0xFFA70,	// $YFFA70 Chip Select Base Address Register 9
		Csor9			= 0xFFA72,	// $YFFA72 Chip Select Option Register 9
		Csbar10			= 0xFFA74,	// $YFFA74 Chip Select Base Address Register 10
		Csor10			= 0xFFA76,	// $YFFA76 Chip Select Option Register 10

		// QSM
		Qsmcr			= 0xffc00,	// $YFFC00 QSM Configuration Register
		Qtest			= 0xffc02,	// $YFFC02 QSM Test Register
		Qilr			= 0xffc04,	// $YFFC04 QSM Interrupt Level Register
		Qivr,						// $YFFC05 QSM Interrupt Vector Register
		NotUsedFFC06	= 0xffc06,
		SciControl0		= 0xffc08,	// $YFFC08 SCI Control Register 0
		SciControl1		= 0xffc0a,	// $YFFC0A SCI Control Register 1
		SciControl1LSB	= 0xffc0b,
		SciStatus		= 0xffc0c,	// $YFFC0C SCI Status Register
		SciData			= 0xffc0e,	// $YFFC0E SCI Data Register
		SciDataLSB		= 0xffc0f,

		Portqs			= 0xffc15,	// $YFFC15 Port QS Data Register
		Pqspar			= 0xffc16,	// $YFFC16 PORT QS Pin Assignment Register
		Ddrqs			= 0xffc17,	// $YFFC17 PORT QS Data Direction Register
		Spcr0			= 0xFFC18,	// $YFFC18 QSPI Control Register 0
		Spcr1			= 0xFFC1a,	// $YFFC1A QSPI Control Register 1
		Spcr2			= 0xFFC1c,	// $YFFC1C QSPI Control Register 2
		Spcr3			= 0xFFC1e,	// $YFFC1E QSPI Control Register 3
		Spsr			= 0xFFC1f,	// $YFFC1F QSPI Status Register

		ReceiveRam0		= 0xffd00,
		TransmitRam0	= 0xffd20,
		CommandRam0		= 0xffd40,
	};
}
