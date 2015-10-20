
#define DEBUG


#include <mcs51/8051.h>

__code char LEDDigit[] = {
	0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 
	0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71, 0x40
};

char screen[4], screen_en;


void delay(char ms)
{
	while(ms) {
		char j;
		for(j = 123; j; --j) (void)0;
		--ms;
	}
}

#ifdef DEBUG
#define LED8_S		P0
#define LED8_B		P2
#else
#define LED8_S		P0
#define LED8_B		P0
#define LED8_S_EN	P1_4
#define LED8_B_EN	P1_5
#define LED8_REVERSE	7
#endif

inline void led_refresh(void)
{
	char i;
	for(i=0; i<sizeof(screen)/sizeof(char); ++i) {
// FIX! 消除重影逻辑[防闪烁]
// (先拉高位线/去除选择, 再修改段线和选择位线)
	LED8_B = ~0U;
#ifdef LED8_B_EN
		LED8_B_EN = 1;
		LED8_B_EN = 0;
#endif
// END FIX!

		if ((screen_en >> i) & 1) LED8_S = LEDDigit[screen[i]];
		else LED8_S = 0;
#ifdef LED8_S_EN
		LED8_S_EN = 1;
		LED8_S_EN = 0;
#endif

#ifdef LED8_REVERSE
		LED8_B = ~(1<<LED8_REVERSE-i);
#else
		LED8_B = ~(1<<i);
#endif
#ifdef LED8_B_EN
		LED8_B_EN = 1;
		LED8_B_EN = 0;
#endif
		delay(1);
	}
}

struct {
	unsigned char TH, TL;
} __code keyc[] = {
	{0, 0},
	{64580/256, 64580 % 256},
	{64684/256, 64684 % 256},
	{64777/256, 64777 % 256},
	{64820/256, 64820 % 256},
	{64898/256, 64898 % 256},
	{64968/256, 64968 % 256},
	{65030/256, 65030 % 256}
};

struct {
	char delay;
	char key;
} __code mus[] = {
// 通电提示音
	{0, -1}, {0, 0},
	{4, 1}, {4, 5},
// 时间到音乐
	{0, -1}, {0, 0},
#include "mus_littlestar.h"
// 结束
	{0, -1},
};

char mus_nidx;

char shining;

__sbit system_status = 1;		// 1 - paused
#define paused system_status
int timeout_s;
char timeout_10ms = -1;

inline
void timeout_screen()
{
	char s = timeout_s % 60;
	char m = timeout_s / 60;
	screen[3] = m / 10;
	screen[2] = m % 10;
	screen[1] = s / 10;
	screen[0] = s % 10;
}


#define resumetimer()		(timeout_10ms = timeout_s>0 ? 99 : -1)
#define starttimer(x)		(timeout_s = (x), timeout_10ms = 0)

#define stop_shining()		(shining = 0,screen_en = 0xf)

char TH1_0, TL1_0;
char press[4];
#define KEY_PRESS	2

void mus_playnote(char key)
{
	if (key > 0) {
		TH1 = TH1_0 = keyc[key].TH;
		TL1 = TL1_0 = keyc[key].TL;
	}
	TR1 = (key > 0);
}

#define SPEAKER_IO	P1_6
#define LED_IO		P1_7

// 实际电路和仿真指示LED接法不同
#ifdef DEBUG
#define led_reset()	(LED_IO = 1)
#else
#define LED_COM_ANODE	1
#define led_reset()	(LED_IO = 0)
#endif

inline void keyhandler_0()
{
	system_status = 1;
	screen_en = 0xf, shining = 0xC;		// 分钟闪烁
	led_reset();				// power led on
}

inline void keyhandler_1()
{
	if (!system_status) starttimer(5 * 60);
	else {
		if (timeout_s < 3540) timeout_s += 60;
		else timeout_s = 3599;		// 防止溢出
	}
}

inline void keyhandler_2()
{
	if (!system_status) starttimer(10 * 60);
	else if (timeout_s > 60) timeout_s -= 60; 
}

inline void keyhandler_3()
{
	if (!system_status) starttimer(20 * 60);
	else stop_shining(), system_status = 0, resumetimer();
}

#define __keyhandler(k, id)	do{\
	if (!k) {	\
		press[id] += (press[id] <= KEY_PRESS);	\
		if (press[id] == KEY_PRESS) {	\
			keyhandler_ ##id();	\
			timeout_screen();	\
		}	\
	} else press[id] = 0;	\
} while(0)


/// 标准10ms中断
void
timer0_isr(void) __interrupt (1) __using(1)
{
	static int mus_beats = 0;

	// 按键逻辑
#ifdef DEBUG
	__keyhandler(P1_0, 0);
	__keyhandler(P1_1, 1);
	__keyhandler(P1_2, 2);
	__keyhandler(P1_3, 3);
#else
	__keyhandler(P2_0, 0);
	__keyhandler(P2_1, 1);
	__keyhandler(P2_2, 2);
	__keyhandler(P2_3, 3);

#endif

	// 倒计时逻辑
	if (!paused) {
		if (timeout_10ms > 0) --timeout_10ms;
		else if (!timeout_10ms) {
			if (!--timeout_s) {
				timeout_10ms = -1;	// disable
				led_reset();		// power led on
				mus_nidx = 5;		// play music
				screen_en = 0xf, shining = 0xF;
			} else timeout_10ms = 99, LED_IO ^= 1;
			timeout_screen();
		}
	}

	// 开始播放音乐
	if (mus[mus_nidx].key >= 0) {
		if (mus_beats >= mus[mus_nidx].delay) {
			mus_nidx++, mus_beats = 0;
			mus_playnote(mus[mus_nidx].key);
			if (mus[mus_nidx].key < 0) stop_shining();
		} else mus_beats++;
	}

	// 屏幕闪烁
	if (shining) {
		static int shining_timer = 0;
		if (shining_timer >= 50) screen_en ^= shining, shining_timer = 0;
		else shining_timer++;
	}

	TH0 = 0xD8, TL0 = 0xF0;			// 10ms
}

/// 方波发生器
void
timer1_isr(void) __interrupt (3)
{
	SPEAKER_IO ^= 1;
	TH1 = TH1_0, TL1 = TL1_0;
}

void main(void)
{
	screen_en = 0xF;

	TMOD = 0x11;
	TH0 = 0xD8, TL0 = 0xF0;			// 10ms
	ET0 = ET1 = EA = 1;
	TR0 = 1;

	P1 = P2 = 0xff;				// IN I/O 先写1

	led_reset();				// power led on

//	timeout_ms = 999, timeout_s = 10;
	mus_nidx = 1;
	while(1) led_refresh();
}