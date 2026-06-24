/********************************************************************
 * COPYRIGHT -- My Homes
 ********************************************************************
 * Program: MHC_Interface
 * File: MHC_InterfaceCyclic.c
 * Author: Administrator
 * Created: September 17, 2012
 ********************************************************************
 * Implementation of program MHC_Interface
 ********************************************************************/

#include <bur/plctypes.h>
#include <sys_lib.h>
#include "MHC_Interface.h"
#include "pic.h"
#include "mmi.h"
#include "ma.h"
#include "alarm.h"
#include <stdio.h>



#ifdef _DEFAULT_INCLUDES
#include <AsDefault.h>
#endif

_GLOBAL		ma_stat_typ			MA_STAT;					/* Actual machine status */
_GLOBAL_RETAIN		ma_fix_typ		MA_FIX;
_GLOBAL     mmi_typ             MMI_DATA;                   /* HMI data interface */
_GLOBAL		BOOL			Set_all_SV_flag;				/* 180326 YJM set all SV parameter */
int queue_analyze_step=0;
int queue_analyze_delay=0;
int queue_analyze_setup_delay =50;
int hotrunerrxthread_step=0;
int hotrunerrxthread_delay=0;
int hotrunertxthread_step=0;
int hotrunertxthread_delay=0;	
int hotrunner_check_temp_setp=0;
int hotrunner_check_temp_delay=0;
int moudlecount_setp = 0; //485 통신 연결시 모듈 갯수 증가시 사용 
int moudlecount =0;//485 통신 연결시 모듈 갯수 증가시 사용 

unsigned char startup_code[7] = {0x02,0x30,0x12,0x30,0x38,0x03,0xAA }; //기본 8존으로 설정 한다. 		/* 171201 YJM bug occur char -> unsigned char */
unsigned char pv_send[5] = {0x02,0x30,0x10,0x03,0x60}; //pv 요청 코 드													/* 171201 YJM bug occur char -> unsigned char */
unsigned char mk_send[5] = {0x02,0x30,0x20,0x03,0x70}; //출력 률 요청  												/* 171201 YJM bug occur char -> unsigned char */
unsigned char used_send[7] = {0x02,0x30,0x30,0x30,0x81,0x03,0x60}; //pv 요청 코 드								/* 171201 YJM bug occur char -> unsigned char */
unsigned char current_send[5] = {0x02,0x30,0x30,0x03,0x60}; //전류 값 요청    											/* 171201 YJM bug occur char -> unsigned char */
unsigned char HMIN_HMAX_send[15] = {0x02,0x30,0x80,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x03,0xB0};	/* 171201 YJM bug occur char -> unsigned char */

char TESTPACKGET[10] = {0x0};
int hot_startupflag = 0;
int setvalue_option =1;

unsigned char Rx_buffer[1024];			//171017 YJM	/* 171201 YJM bug occur char -> unsigned char */
unsigned char Rx_tmp_buffer[1024];	//171017 YJM	/* 171201 YJM bug occur char -> unsigned char */

int rxthbuf_count=0;
int startupfuntion_flag =0;
int startupfuntion_tamp_flag=0;
int startupfuntion_step = 0;
int startupfuntion_q = 0;
int setdata_change_q=0;
int setdata_change_step =0;
int setdata_change_flag =0;
int setdata_change_delay_count=0;
int setdata_change_delay_maxcount=0;
int old_zones=0;
int	setdata_all_change_flag=0;


/* ---------------------------------------------------------------
 * MHC communication state manager
 * - Startup uses handshake + retry.
 * - Runtime polling remains non-blocking and uses watchdog recovery.
 * - Cycle counts assume a 10 ms cyclic task. Adjust constants if task time differs.
 * --------------------------------------------------------------- */
#define MHC_POWERON_WAIT_CYCLE      300     /* 3 sec @ 10 ms */
#define MHC_STARTUP_ACK_TIMEOUT     100     /* 1 sec @ 10 ms */
#define MHC_STARTUP_RETRY_MAX       5
#define MHC_COMM_LOST_LIMIT         10
#define MHC_PACKET_GAP              2

typedef enum {
	MHC_STATE_POWERON_WAIT = 0,
	MHC_STATE_SEND_STARTUP,
	MHC_STATE_WAIT_STARTUP_ACK,
	MHC_STATE_STARTUP_SETTING,
	MHC_STATE_RUN,
	MHC_STATE_COMM_RECOVERY
} MHC_STATE_TYP;

MHC_STATE_TYP mhc_state = MHC_STATE_POWERON_WAIT;

int mhc_poweron_wait_cnt = 0;
int mhc_startup_ack_timeout = 0;
int mhc_startup_retry = 0;
int mhc_packet_gap_cnt = 0;
int mhc_startup_ack_received = 0;
int mhc_comm_lost_count = 0;
int mhc_rx_ok_this_cycle = 0;

void MHC_StateManager(void);
void MHC_SendStartupCode(void);
void MHC_ResetRxBuffer(void);

void MHC_Alarm_Process(void);
void hotrunerrxthread(void);
void hotrunertxthread(void);
void rx_data_analyze(unsigned char * data,int size);
void setdata_change(void);
void startupfuntion(char * data);
int asciihex2dec(unsigned char * data,int size);
void dec2asciihex(unsigned int data,unsigned char * dsc,int size);
unsigned char dec2hex (unsigned int data);
unsigned char LRCcal(char* data,int size);
UINT send_dvf_tx_data(char * data,int size);

void _CYCLIC MHC_InterfaceCyclic(void)
{
	/* RX must run every cycle, even during startup/setdata writes. */
	mhc_rx_ok_this_cycle = 0;
	hotrunerrxthread();

	MHC_StateManager();

	if(REC_DATA.MHC_CTRL.EnableCommunication){  //KBH 151210 수정 //170313 YJM REC데이터로 변경
		MHC_Alarm_Process();	
	}

	/* Original condition used AND between AUTO and SEMI_AUTO, which can never be true.
	 * Logic is left effectively unchanged by the EnableCommunication alarm call above.
	 */
	if(REC_DATA.MHC_CTRL.Zone_Set != old_zones){	//160624 YJM
		hot_startupflag = 0;					//160624 YJM
		mhc_state = MHC_STATE_COMM_RECOVERY;
	}
	old_zones = REC_DATA.MHC_CTRL.Zone_Set;		//160624 YJM
}

void MHC_StateManager(void)
{
	switch(mhc_state)
	{
		case MHC_STATE_POWERON_WAIT:
			mhc_poweron_wait_cnt++;
			if(mhc_poweron_wait_cnt >= MHC_POWERON_WAIT_CYCLE)
			{
				mhc_poweron_wait_cnt = 0;
				mhc_startup_retry = 0;
				mhc_state = MHC_STATE_SEND_STARTUP;
			}
			break;

		case MHC_STATE_SEND_STARTUP:
			if(mhc_packet_gap_cnt < MHC_PACKET_GAP)
			{
				mhc_packet_gap_cnt++;
				break;
			}
			mhc_packet_gap_cnt = 0;
			MHC_SendStartupCode();
			mhc_startup_ack_received = 0;
			mhc_startup_ack_timeout = 0;
			mhc_state = MHC_STATE_WAIT_STARTUP_ACK;
			break;

		case MHC_STATE_WAIT_STARTUP_ACK:
			if(mhc_startup_ack_received)
			{
				mhc_startup_ack_received = 0;
				hot_startupflag = 1;
				startupfuntion_flag = 1;
				startupfuntion_step = 0;
				startupfuntion_q = 0;
				mhc_state = MHC_STATE_STARTUP_SETTING;
			}
			else
			{
				mhc_startup_ack_timeout++;
				if(mhc_startup_ack_timeout >= MHC_STARTUP_ACK_TIMEOUT)
				{
					mhc_startup_ack_timeout = 0;
					mhc_startup_retry++;
					MHC_ResetRxBuffer();
					if(mhc_startup_retry >= MHC_STARTUP_RETRY_MAX)
					{
						mhc_startup_retry = 0;
						mhc_state = MHC_STATE_POWERON_WAIT;
					}
					else
					{
						mhc_state = MHC_STATE_SEND_STARTUP;
					}
				}
			}
			break;

		case MHC_STATE_STARTUP_SETTING:
			/* Original startup setting sequence is preserved. */
			startupfuntion((char *)Rx_buffer);
			if(startupfuntion_flag == 0)
			{
				mhc_comm_lost_count = 0;
				mhc_state = MHC_STATE_RUN;
			}
			break;

		case MHC_STATE_RUN:
			if(setdata_change_flag)
			{
				setdata_change();
			}
			else
			{
				hotrunertxthread();
			}

			/* Runtime polling is not handshake-blocked. Only recover after repeated no-response cycles. */
			if(mhc_rx_ok_this_cycle)
			{
				mhc_comm_lost_count = 0;
			}
			else
			{
				mhc_comm_lost_count++;
				if(mhc_comm_lost_count >= MHC_COMM_LOST_LIMIT)
				{
					mhc_comm_lost_count = 0;
					hot_startupflag = 0;
					MHC_ResetRxBuffer();
					mhc_state = MHC_STATE_COMM_RECOVERY;
				}
			}
			break;

		case MHC_STATE_COMM_RECOVERY:
			startupfuntion_flag = 0;
			setdata_change_flag = 0;
			startupfuntion_step = 0;
			startupfuntion_q = 0;
			hotrunertxthread_step = 0;
			MHC_ResetRxBuffer();
			mhc_startup_retry = 0;
			mhc_state = MHC_STATE_SEND_STARTUP;
			break;
	}
}

void MHC_SendStartupCode(void)
{
	unsigned char changedata[4];
	unsigned char LRCresult;

	if(MA_FIX.OPT.MHC_Page_Enable == 1){
		if(REC_DATA.MHC_CTRL.EnableCommunication == 1){	//170313 YJM REC데이터로 변경
			dec2asciihex(REC_DATA.MHC_CTRL.Zone_Set,changedata,2);
			memcpy(&(startup_code[3]),changedata,2);
			LRCresult = LRCcal((char *)startup_code,7);
			startup_code[6] = LRCresult;
			send_dvf_tx_data((char *)startup_code,7);
		}
	}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
		dec2asciihex(MA_FIX.OPT.SnExtTempZone,changedata,2);
		memcpy(&(startup_code[3]),changedata,2);
		LRCresult = LRCcal((char *)startup_code,7);
		startup_code[6] = LRCresult;
		send_dvf_tx_data((char *)startup_code,7);
	}
}

void MHC_ResetRxBuffer(void)
{
	memset(Rx_buffer,0,sizeof(Rx_buffer));
	memset(Rx_tmp_buffer,0,sizeof(Rx_tmp_buffer));
	rxthbuf_count = 0;
	Receiver.step = FRMR_READ;
	Receiver.buffer = 0;
	Receiver.buffer_length = 0;
	Receiver.status = 0;
}

void MHC_Alarm_Process(void){
	int i;
	MHC_Zone_Set = REC_DATA.MHC_CTRL.Zone_Set; //KBH 151210 수정 
	if(MA_FIX.OPT.MHC_Page_Enable == 1){
		for( i = 0; i < MHC_Zone_Set; i++ )
		{
			//	if (MHC_Zone_Enable[i]){		//170313 YJM REC데이터로 변경
			if (REC_DATA.MHC[i].Enable){
				if(MHC_PV_Value[i] & 0x8000)
				{
					if( MHC_PV_Value[i] == 0x8000 ) SET_MHC_ALARM(i,S_ALM_MHC_SENEOR_OPEN)	else RESET_MHC_ALARM(i,S_ALM_MHC_SENEOR_OPEN)	// Sensor Open
					if( MHC_PV_Value[i] == 0x8001 ) SET_MHC_ALARM(i,S_ALM_MHC_SENEOR_SHOT)	else RESET_MHC_ALARM(i,S_ALM_MHC_SENEOR_SHOT)	// Sensor Shot
					if( MHC_PV_Value[i] == 0x8002 ) SET_MHC_ALARM(i,S_ALM_MHC_SENSOR_ABNORMALCNN)	else RESET_MHC_ALARM(i,S_ALM_MHC_SENSOR_ABNORMALCNN)	// Sensor abnormal connection
				}else {
					RESET_MHC_ALARM(i,S_ALM_MHC_SENEOR_OPEN)	// Sensor Open
					RESET_MHC_ALARM(i,S_ALM_MHC_SENEOR_SHOT)	// Sensor Shot
					RESET_MHC_ALARM(i,S_ALM_MHC_SENSOR_ABNORMALCNN)	// Sensor abnormal connection
				}
				/* 온도 상한/하한 알람 */
				// 하한 알람
				if(MMI_DATA.Mode == 1 || MMI_DATA.Mode == 2) //160309_leejs
				{
					if( (MHC_PV_Value[i]/10) < (REC_DATA.MHC[i].SetValue - REC_DATA.MHC[i].MinSet) ) SET_MHC_ALARM(i,S_ALM_MHC_PV_LOW)	else RESET_MHC_ALARM(i,S_ALM_MHC_PV_LOW)	// Hotrunner PV Temperature Low
					// 상한 알람
					if( (MHC_PV_Value[i]/10) > (REC_DATA.MHC[i].SetValue + REC_DATA.MHC[i].MaxSet) ) SET_MHC_ALARM(i,S_ALM_MHC_PV_HIGH)	else RESET_MHC_ALARM(i,S_ALM_MHC_PV_HIGH)	// Hotrunner PV Temperature High
				}	
			}else {
				MHC_PV_Value[i] = 0;
				MHC_PV_MKValue[i] = 0;
				MHC_PV_HeaterCurrent[i]=0;
			}
		}
	}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
		

	}
}

void hotrunerrxthread(){
	unsigned char * data;
	int comdid;
	int q;
	hotrunerrxthread_delay = 0;

	switch(Receiver.step)
	{
		case FRMR_READ:
			Receiver.FRM_read_0.enable = 1;
			Receiver.FRM_read_0.ident = Receiver.FRM_xopen_0.ident;
			FRM_read(&Receiver.FRM_read_0); 
			if (Receiver.FRM_read_0.status == 0)
				Receiver.step = FRMR_COPY_RD_DATA;								/*system returned a valid data --> next step*/
			else if (Receiver.FRM_read_0.status == frmERR_NOINPUT)
				Receiver.step = FRMR_READ;										/*no data available --> call again*/
			else if (Receiver.FRM_read_0.status == frmERR_INPUTERROR)
				Receiver.step = FRMR_RBUF;										/*received Frame with defective characters, skip data*/
			else
				Receiver.step = FRMR_ERROR;
			
			Receiver.buffer 		= Receiver.FRM_read_0.buffer;
			Receiver.buffer_length 	= Receiver.FRM_read_0.buflng;
			Receiver.status			= Receiver.FRM_read_0.status;

			break;
		
		case FRMR_COPY_RD_DATA:
			memcpy(Rx_tmp_buffer, Receiver.buffer, Receiver.buffer_length);    /*copy and evaluate read data*/
			for(q=0;q<Receiver.buffer_length;q++){
				if((rxthbuf_count>1)&&(Rx_buffer[rxthbuf_count -1]==0x03)){
					Rx_buffer[rxthbuf_count] = Rx_tmp_buffer[q];
					rxthbuf_count++;		
					//					if(LRCcal(Rx_buffer,rxthbuf_count)!=(unsigned char)Rx_buffer[rxthbuf_count-1]){
					if((LRCcal(Rx_buffer,rxthbuf_count)!=Rx_buffer[rxthbuf_count-1]) && (LRCcal(Rx_buffer,rxthbuf_count)!=(Rx_buffer[rxthbuf_count-1] | 0x20 ))){		//171201 YJM modified(not very good source and for narae)
						//	printf("LRC falut \n");
						memset(Rx_buffer,0,rxthbuf_count);
						rxthbuf_count = 0;	
						Receiver.step = FRMR_RBUF;
						break;
					}
					data = Rx_buffer;
					//start of frame 
					if(data[0]==0x02){
						comdid = asciihex2dec((unsigned char *)&data[1],1);
						if(data[2]==0x12 && data[4]==0x03){
							mhc_startup_ack_received = 1;
							startupfuntion_tamp_flag = 0;
							rxthbuf_count = 0;	
						}else {
							rx_data_analyze(Rx_buffer,rxthbuf_count);
							mhc_rx_ok_this_cycle = 1;
						}
					}
					memset(Rx_buffer,0,rxthbuf_count);
					rxthbuf_count = 0;						 
				}else {
					Rx_buffer[rxthbuf_count] = Rx_tmp_buffer[q];
					rxthbuf_count++;
				}
			}
			Receiver.step = FRMR_RBUF;
			break;
		case FRMR_RBUF:
			Receiver.FRM_rbuf_0.enable 	= 1;
			Receiver.FRM_rbuf_0.ident 	= Receiver.FRM_xopen_0.ident;			/*ident from FRM_xopen()*/
			Receiver.FRM_rbuf_0.buffer 	= Receiver.buffer;						/*read buffer*/
			Receiver.FRM_rbuf_0.buflng 	= Receiver.buffer_length;				/*length of sendbuffer*/
			FRM_rbuf(&Receiver.FRM_rbuf_0); 
			
			if (Receiver.FRM_rbuf_0.status == 0)
			{
				Receiver.step = FRMR_FINISH;										/*read again*/
			}
			else if (Receiver.FRM_rbuf_0.status == BUSY1)
				Receiver.step = FRMR_RBUF;										/*operation not finished yet --> call again*/
			else if (Receiver.FRM_rbuf_0.status == frmERR_INVALIDBUFFER)					
				Receiver.step = FRMR_READ;										/*buffer is invalid --> read again*/
			else
				Receiver.step = FRMR_ERROR;
			
			Receiver.status		= Receiver.FRM_rbuf_0.status;
			break;
		case FRMR_ERROR: /*--- error handling*/
			
			Receiver.step = FRMR_READ;
			
			break;
		case FRMR_FINISH:
			Receiver.step = FRMR_READ;
			break;
	}

}
//serial send logic 
void hotrunertxthread(){
	int uv_MaxHotrunNum_tamp;
	char send_set_packet[11];
	unsigned char LRCresult;
	unsigned char tamp_buff[12];			//171201 YJM char - > unsigned char
	unsigned char comdid;			//171201 YJM char - > unsigned char
	int slaveid;
	int setvalue;
	int q;
	int j;
	int FP_setzone;
	unsigned char changedata[4];
	if(MA_FIX.OPT.Sbchangsung_Enable == 0){ // KBH 151204 외부 온도 옵션이 0 이면 화인 전자
		setvalue_option = 10;
	}
	if(MA_FIX.OPT.Sbchangsung_Enable == 2){  // KBH 151204 외부 온도 옵션이 2 이면 한영 넉스
		setvalue_option = 1;
	}
	if(MA_FIX.OPT.MHC_Page_Enable == 1){
		uv_MaxHotrunNum_tamp = REC_DATA.MHC_CTRL.Zone_Set;
	}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
		uv_MaxHotrunNum_tamp = MA_FIX.OPT.SnExtTempZone; //KBH 160202 수정 
	}
	
	switch (hotrunertxthread_step)
	{
		case 0:
			hotrunertxthread_delay++;
			if(hotrunertxthread_delay<1){
				break;
			}			
			if(hot_startupflag == 1)
			{
				hotrunertxthread_step = 1; 
				hotrunertxthread_delay=0;   
			}
			else
			{
				/* Startup request is handled only by MHC_StateManager. */
				hotrunertxthread_delay = 0;
			}
			break;
		case 1:
			hotrunertxthread_delay++;
			if(hotrunertxthread_delay<2){		//171017 YJM 5 -> 2
				break;
			}
			moudlecount = uv_MaxHotrunNum_tamp/Node_Max_zone;
			moudlecount++;
			pv_send[1] = 0x30 + moudlecount_setp;          //commid
			pv_send[4] = 0x60 + moudlecount_setp;         //LRC
			send_dvf_tx_data(pv_send,5);
			hotrunertxthread_step++;
			hotrunertxthread_delay =0;
			break;
		case 2:
			hotrunertxthread_delay++;
			if(hotrunertxthread_delay<2){		//171017 YJM 5 -> 2
				break;
			}
			mk_send[1] = 0x30 + moudlecount_setp;       //commid
			mk_send[4] = 0x70 + moudlecount_setp;      //LRC
			send_dvf_tx_data(mk_send,5);
			hotrunertxthread_step++;
			hotrunertxthread_delay =0;
			break;
		case 3:
			hotrunertxthread_delay++;
			if(hotrunertxthread_delay<2){		//171017 YJM 5 -> 2
				break;
			}
			current_send[1] = 0x30 + moudlecount_setp;   //commid
			current_send[4] = 0x60 + moudlecount_setp;  //LRC
			send_dvf_tx_data(current_send,5);
			moudlecount_setp++;
			if(moudlecount==moudlecount_setp){
				hotrunertxthread_step++;
				moudlecount =0;
				moudlecount_setp = 0;
			}else{
				hotrunertxthread_step = 2;
			}
			hotrunertxthread_delay =0;
			break;
		case 4:
			hotrunertxthread_delay++;
			if(hotrunertxthread_delay<2){			//171017 YJM 5 -> 2
			break;
			}
			//바뀐 값이 있을때는 send  
			for(j=0;j<uv_MaxHotrunNum_tamp;j++){
			/* 180326 YJM Set all SV */
			if(Set_all_SV_flag)
			{
			temp_set_tempvalue[j] = REC_DATA.MHC[j].SetValue;
			}
			else
			{
			if(MA_FIX.OPT.MHC_Page_Enable == 1){
			if(temp_set_tempvalue[j]!=REC_DATA.MHC[j].SetValue){   //금형 Foof변수
			setdata_change_flag = 1;
			setdata_change_step = 0;
			}else if(temp_used_tempvalue[j]!=REC_DATA.MHC[j].Enable){	//170313 YJM REC데이터로 변경
			setdata_change_flag = 1;
			setdata_change_step = 0;
			}else if(temp_setmin_tempvalue[j]!=REC_DATA.MHC[j].MinSet){
			setdata_change_flag = 1;
			setdata_change_step = 0;
			}else if(temp_setmax_tempvalue[j]!=REC_DATA.MHC[j].MaxSet){
			setdata_change_flag = 1;
			setdata_change_step = 0;
			}
			}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
			if(temp_set_tempvalue[j]!=REC_DATA.HC.Zone[9+j].ST){   //금형 Foof변수
			setdata_change_flag = 1;
			setdata_change_step = 0;
			}else if(temp_used_tempvalue[j]!=REC_DATA.HC.Zone[9+j].SbOn){
			setdata_change_flag = 1;
			setdata_change_step = 0;
			}else if(temp_setmin_tempvalue[j]!=REC_DATA.HC.Zone[9+j].STnTol){
			setdata_change_flag = 1;
			setdata_change_step = 0;
			}else if(temp_setmax_tempvalue[j]!=REC_DATA.HC.Zone[9+j].STpTol){
			setdata_change_flag = 1;
			setdata_change_step = 0;
			}else if(MHC_temp_FPpass[j]!=REC_DATA.MHC_FPpass[j]){//KBH160203
			setdata_change_flag = 1;
			setdata_change_step = 0;
			}
			}
			}
			}
			if(Set_all_SV_flag)	/* 180326 YJM Set all SV */
			{
				Set_all_SV_flag = 0;
				setdata_change_flag = 1;
				setdata_change_step = 0;
				setdata_all_change_flag = 1;
			}
			hotrunertxthread_step++;
			hotrunertxthread_delay =0;
			break;
		case 5: //KBH160203
			hotrunertxthread_delay++;
			if(hotrunertxthread_delay<2){		//171017 YJM 5 -> 2
			break;
			}
			if(MA_FIX.OPT.MHC_Page_Enable == 2){
				for(j=0;j<uv_MaxHotrunNum_tamp;j++){
					if(!REC_DATA.HC.Zone[9+j].SbOn){
						ACT_DATA.Zone[9+j].AT =0; //KBH 20160210 
					}else if(REC_DATA.HC.Zone[9+j].SbOn && REC_DATA.MHC_FPpass[j]){
						comdid = 0;
						slaveid = j-(comdid*Node_Max_zone);  	 
						FP_setzone = REC_DATA.MHC_FPpass[j]-1;
						if(FP_setzone == 6){
							setvalue = ACT_DATA.System.ATOil/setvalue_option;
							ACT_DATA.Zone[9+j].AT = setvalue;
						}else {
							setvalue = (ACT_DATA.Zone[FP_setzone].AT)/setvalue_option;
							ACT_DATA.Zone[9+j].AT = setvalue;
						}
					
						dec2asciihex(comdid,changedata,1);
						tamp_buff[0] = changedata[0];
						tamp_buff[1] = 'C';
						dec2asciihex(slaveid,changedata,2);
						memcpy(&(tamp_buff[2]),changedata,2);
						dec2asciihex(setvalue,changedata,4);
						memcpy(&(tamp_buff[4]),changedata,4);
						memcpy(&send_set_packet[1],tamp_buff,8);
						send_set_packet[0] = 0x02; //START
						send_set_packet[9] = 0x03; //END
						LRCresult = LRCcal(send_set_packet,11);
						send_set_packet[10] = LRCresult;
						if(send_dvf_tx_data(send_set_packet,11)){
		
						}else {
							j--;
						}
					}
				}
			}
			hotrunertxthread_step = 0;
			hotrunertxthread_delay =0;
			break;
	}
}

void rx_data_analyze(unsigned char * data,int size){
	int q;
	int k;
	int comdid;
	int newcnt;
	int slaveid;
	int value;
	comdid = asciihex2dec((unsigned char *)&data[1],1);
	for(q=0;q<size-1;q++){
		if(data[q]==0x62){
			q = q+1;	
			newcnt = asciihex2dec((unsigned char *)&data[q],2);// 카운터 개수
			q = q+2;
			for(k=0;k<newcnt;k++){
				slaveid = asciihex2dec((unsigned char *)&data[q],2);
				//slaveid = slaveid + (comdid*Node_Max_zone);	//160704 YJM
				q= q+2;
				value = asciihex2dec((unsigned char *)&data[q],4);
				q = q+4;
							    
				if(MA_FIX.OPT.MHC_Page_Enable == 1){
					if(MA_FIX.OPT.Sbchangsung_Enable == 0){   //KBH 151204 수정 
						REC_DATA.MHC[slaveid].SetValue= value;
					}else if(MA_FIX.OPT.Sbchangsung_Enable == 1){
						REC_DATA.MHC[slaveid].SetValue= value/10;
					}else if(MA_FIX.OPT.Sbchangsung_Enable == 2){
						REC_DATA.MHC[slaveid].SetValue= value/10;
					}
				}else if(MA_FIX.OPT.MHC_Page_Enable == 2 ){ //KBH 160202 수정 && slaveid <=7 제거 
					REC_DATA.HC.Zone[9+slaveid].ST= value*setvalue_option;
				}																	 								     
			}	
			break;
		}
	}
	//serch find 0x63 pv value
	for(q=0;q<size-1;q++){
		if(data[q]==0x63){
			q = q+1;
			newcnt = asciihex2dec((unsigned char *)&data[q],2);// 카운터 개수
			q = q+2;
			//slavid & data get 
			for(k=0;k<newcnt;k++){
				slaveid = asciihex2dec((unsigned char *)&data[q],2);
				//slaveid = slaveid + (comdid*Node_Max_zone);		//160704 YJM
				q= q+2;
				value =  asciihex2dec((unsigned char *)&data[q],4);
				q = q+4;
				//Debug code
				// printf("slaveid = %d , value = %d \n",slaveid,value);
				if(MA_FIX.OPT.MHC_Page_Enable == 1){
					if(REC_DATA.MHC[slaveid].Enable){//170313 YJM REC데이터로 변경
						MHC_PV_Value[slaveid] = value;
					}else {
						MHC_PV_Value[slaveid] = 0;
					}
				}else if(MA_FIX.OPT.MHC_Page_Enable == 2 ){ //KBH160203
					if(REC_DATA.HC.Zone[9+slaveid].SbOn && (!REC_DATA.MHC_FPpass[slaveid])){//KBH160203
						ACT_DATA.Zone[9+slaveid].AT = (INT)value;
					}else {
						//KBH20160210
					}
				}
			}	 
			break; 		   				     
		}
	}
	for(q=0;q<size-1;q++){
		if(data[q]==0x70){
			setvalue_option = 10; // auto config
			q = q+1;
			newcnt = asciihex2dec((unsigned char *)&data[q],2);// 카운터 개수
			q = q+2;
			//slavid & data get 
			for(k=0;k<newcnt;k++){
				slaveid = asciihex2dec((unsigned char *)&data[q],2);
				//slaveid = slaveid + (comdid*Node_Max_zone);		//160704	YJM
				q= q+2;
				if(MA_FIX.OPT.MHC_Page_Enable == 1){
					value =  asciihex2dec((unsigned char *)&data[q],4);
				}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
					value = 0;
				}
				q = q+4;
				MHC_PV_MKValue[slaveid] = value;																 								     
			}	 
			break; 		   				     		   				        
		   				        
		}
	}	
	//serch find 0x71 current value
	for(q=0;q<size-1;q++){
		if(data[q]==0x71){
			setvalue_option = 10; // auto config
			q = q+1;
			newcnt = asciihex2dec((unsigned char *)&data[q],2);// 카운터 개수
			q = q+2;
			//slavid & data get 
			for(k=0;k<newcnt;k++){
				slaveid = asciihex2dec((unsigned char *)&data[q],2);
				//slaveid = slaveid + (comdid*Node_Max_zone);	//160704 YJM
				q= q+2;
				if(MA_FIX.OPT.MHC_Page_Enable == 1){
					value =  asciihex2dec((unsigned char *)&data[q],4);
				}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
					value = 0;
				}
				q = q+4;

				MHC_PV_HeaterCurrent[slaveid] = value;																	 								     
			}	 
			break; 		   				         			         
		}
	}
	
}

void setdata_change(){
	int uv_MaxHotrunNum_tamp;		
	unsigned char tamp_buff[12];		//171201 YJM char - > unsigned char
	unsigned char changedata[4];
	int comdid; 
	int slaveid;
	int setvalue;
	char send_set_packet[11];
	unsigned char LRCresult;
	if(MA_FIX.OPT.MHC_Page_Enable == 1){
		uv_MaxHotrunNum_tamp = (int)REC_DATA.MHC_CTRL.Zone_Set;
	}else if(MA_FIX.OPT.MHC_Page_Enable == 2){ //KBH 160202 수정 
		uv_MaxHotrunNum_tamp = MA_FIX.OPT.SnExtTempZone;
	}
	switch(setdata_change_step){
		case 0:
			if(setdata_change_delay_count<setdata_change_delay_maxcount){
				setdata_change_delay_count++;
				break;
			}
			setdata_change_delay_count = 0;
			/* 180326 YJM Add if */
			if(setdata_all_change_flag)
			{
				comdid = 0;
				slaveid = 255; // 255 -> FF
				setvalue = (REC_DATA.MHC[0].SetValue*10/setvalue_option);
				dec2asciihex(comdid,changedata,1);
				tamp_buff[0] = changedata[0];
				tamp_buff[1] = 'B';
				dec2asciihex(slaveid,changedata,2);
				memcpy(&(tamp_buff[2]),changedata,2);
				dec2asciihex(setvalue,changedata,4);
				memcpy(&(tamp_buff[4]),changedata,4);
				memcpy(&send_set_packet[1],tamp_buff,8);
				send_set_packet[0] = 0x02; //START
				send_set_packet[9] = 0x03; //END
				LRCresult = LRCcal(send_set_packet,11);
				send_set_packet[10] = LRCresult;
				if(send_dvf_tx_data(send_set_packet,11))
				{
					setdata_change_q = uv_MaxHotrunNum_tamp;
					setdata_change_delay_maxcount = 30;
					setdata_all_change_flag = 0;
				}
				else
					setdata_change_delay_maxcount = 0;
			}
			else
			{
				if((temp_set_tempvalue[setdata_change_q]!=REC_DATA.MHC[setdata_change_q].SetValue)&& (MA_FIX.OPT.MHC_Page_Enable == 1)){   //금형 Foof변수
					//comdid = setdata_change_q/Node_Max_zone;		//160704 YJM
					comdid = 0;	//160704
					slaveid = setdata_change_q-(comdid*Node_Max_zone);  	          
					setvalue = (REC_DATA.MHC[setdata_change_q].SetValue*10)/setvalue_option;
					dec2asciihex(comdid,changedata,1);
					tamp_buff[0] = changedata[0];
					tamp_buff[1] = 'B';
					dec2asciihex(slaveid,changedata,2);
					memcpy(&(tamp_buff[2]),changedata,2);
					dec2asciihex(setvalue,changedata,4);
					memcpy(&(tamp_buff[4]),changedata,4);
					memcpy(&send_set_packet[1],tamp_buff,8);
					send_set_packet[0] = 0x02; //START
					send_set_packet[9] = 0x03; //END
					LRCresult = LRCcal(send_set_packet,11);
					send_set_packet[10] = LRCresult;
					if(send_dvf_tx_data(send_set_packet,11)){
						temp_set_tempvalue[setdata_change_q]=REC_DATA.MHC[setdata_change_q].SetValue;
						setdata_change_q++;
						setdata_change_delay_maxcount = 30;		//171201 YJM 10 -> 30
					}else {
						setdata_change_delay_maxcount = 0;
					}
				} else if ((temp_set_tempvalue[setdata_change_q]!=REC_DATA.HC.Zone[9+setdata_change_q].ST)&& (MA_FIX.OPT.MHC_Page_Enable == 2)){
					//comdid = setdata_change_q/Node_Max_zone;	//160704 YJM
					comdid = 0;	//160704
					slaveid = setdata_change_q-(comdid*Node_Max_zone);  	          
					setvalue = (REC_DATA.HC.Zone[9+setdata_change_q].ST)/setvalue_option;
					dec2asciihex(comdid,changedata,1);
					tamp_buff[0] = changedata[0];
					tamp_buff[1] = 'B';
					dec2asciihex(slaveid,changedata,2);
					memcpy(&(tamp_buff[2]),changedata,2);
					dec2asciihex(setvalue,changedata,4);
					memcpy(&(tamp_buff[4]),changedata,4);
					memcpy(&send_set_packet[1],tamp_buff,8);
					send_set_packet[0] = 0x02; //START
					send_set_packet[9] = 0x03; //END
					LRCresult = LRCcal(send_set_packet,11);
					send_set_packet[10] = LRCresult;
					if(send_dvf_tx_data(send_set_packet,11)){
						temp_set_tempvalue[setdata_change_q]=REC_DATA.HC.Zone[9+setdata_change_q].ST;
						setdata_change_q++;
						setdata_change_delay_maxcount = 30;	//171201 YJM 10 -> 30
					}else {
						setdata_change_delay_maxcount = 0;
					}
				}
				else {
					setdata_change_delay_maxcount = 0;
					setdata_change_q++;
				}
			}
			if(setdata_change_q>=uv_MaxHotrunNum_tamp){
				//setdata_change_flag = 0;
				setdata_change_step++;
				setdata_change_q = 0;
				setdata_change_delay_maxcount = 0;
			}
			break;
		case 1:
			setdata_change_delay_count = 0;
			if((temp_used_tempvalue[setdata_change_q] != REC_DATA.MHC[setdata_change_q].Enable)&& (MA_FIX.OPT.MHC_Page_Enable == 1)){   //금형 Foof변수 //170313 YJM REC데이터로 변경
				//comdid = setdata_change_q/Node_Max_zone;	//160704 YJM
				comdid = 0;		//160704 YJM
				slaveid = setdata_change_q-(comdid*Node_Max_zone); 	    	           
				dec2asciihex(comdid,changedata,1);
				tamp_buff[0] = changedata[0]; 
				if(REC_DATA.MHC[setdata_change_q].Enable){//170313 YJM REC데이터로 변경
					tamp_buff[1] = 0x81;
				}else{
					tamp_buff[1] = 0x82;
				}
				dec2asciihex(slaveid,changedata,2);
				memcpy(&(tamp_buff[2]),changedata,2);
				memcpy(&(used_send[1]),tamp_buff,4);
				LRCresult = LRCcal(used_send,7);
				used_send[6] = LRCresult;
				if(send_dvf_tx_data(used_send,7)){
					temp_used_tempvalue[setdata_change_q]=REC_DATA.MHC[setdata_change_q].Enable;//170313 YJM REC데이터로 변경
					setdata_change_q++;
				}else {
					setdata_change_delay_maxcount = 0;
				}
			} else if ((temp_used_tempvalue[setdata_change_q]!=REC_DATA.HC.Zone[9+setdata_change_q].SbOn)&& (MA_FIX.OPT.MHC_Page_Enable == 2)){
				comdid = setdata_change_q/Node_Max_zone;
				slaveid = setdata_change_q-(comdid*Node_Max_zone); 	    	           
				dec2asciihex(comdid,changedata,1);
				tamp_buff[0] = changedata[0]; 
				if(REC_DATA.HC.Zone[9+setdata_change_q].SbOn){
					tamp_buff[1] = 0x81;
				}else{
					tamp_buff[1] = 0x82;
				}
				dec2asciihex(slaveid,changedata,2);
				memcpy(&(tamp_buff[2]),changedata,2);
				memcpy(&(used_send[1]),tamp_buff,4);
				LRCresult = LRCcal(used_send,7);
				used_send[6] = LRCresult;
				if(send_dvf_tx_data(used_send,7)){
					temp_used_tempvalue[setdata_change_q]=REC_DATA.HC.Zone[9+setdata_change_q].SbOn;
					setdata_change_q++;
				}else {
					setdata_change_delay_maxcount = 0;
				}
			}else {
				setdata_change_delay_maxcount = 0;
				setdata_change_q++;
			}
			if(setdata_change_q>=uv_MaxHotrunNum_tamp){
				//setdata_change_flag = 0;
				setdata_change_step++;
				setdata_change_q = 0;
				setdata_change_delay_maxcount = 0;
			}			
			break;
		case 2:
			setdata_change_delay_count = 0;
			if(((temp_setmin_tempvalue[setdata_change_q] != REC_DATA.MHC[setdata_change_q].MinSet) || 
				(temp_setmax_tempvalue[setdata_change_q] != REC_DATA.MHC[setdata_change_q].MaxSet))&& 
			(MA_FIX.OPT.MHC_Page_Enable == 1)){   //금형 Foof변수
				//comdid = setdata_change_q/Node_Max_zone;	//160704 YJM
				comdid = 0;		//160704
				slaveid = setdata_change_q-(comdid*Node_Max_zone); 	    	           
				dec2asciihex(comdid,changedata,1);
				tamp_buff[0] = changedata[0];
				tamp_buff[1] = 0x80;
				dec2asciihex(slaveid,changedata,2);
				memcpy(&(tamp_buff[2]),changedata,2);
				dec2asciihex((REC_DATA.MHC[setdata_change_q].MinSet*10),changedata,4);
				memcpy(&(tamp_buff[4]),changedata,4);
				dec2asciihex((REC_DATA.MHC[setdata_change_q].MaxSet*10),changedata,4);
				memcpy(&(tamp_buff[8]),changedata,4);
				memcpy(&HMIN_HMAX_send[1],tamp_buff,12);
				HMIN_HMAX_send[0] = 0x02; //START
				HMIN_HMAX_send[13] = 0x03; //END
				LRCresult = LRCcal(HMIN_HMAX_send,15);
				HMIN_HMAX_send[14] = LRCresult;
				if(send_dvf_tx_data(HMIN_HMAX_send,15)){
					temp_setmin_tempvalue[setdata_change_q]=REC_DATA.MHC[setdata_change_q].MinSet;
					temp_setmax_tempvalue[setdata_change_q]=REC_DATA.MHC[setdata_change_q].MaxSet;
					setdata_change_q++;
				}else {
					setdata_change_delay_maxcount = 0;
				}
			} else if(((temp_setmin_tempvalue[setdata_change_q] !=REC_DATA.HC.Zone[9+setdata_change_q].STnTol) || 
				(temp_setmax_tempvalue[setdata_change_q] != REC_DATA.HC.Zone[9+setdata_change_q].STpTol)) && 
			(MA_FIX.OPT.MHC_Page_Enable == 2)){
				comdid = setdata_change_q/Node_Max_zone;
				slaveid = setdata_change_q-(comdid*Node_Max_zone); 	    	           
				dec2asciihex(comdid,changedata,1);
				tamp_buff[0] = changedata[0];
				tamp_buff[1] = 0x80;
				dec2asciihex(slaveid,changedata,2);
				memcpy(&(tamp_buff[2]),changedata,2);
				dec2asciihex((REC_DATA.HC.Zone[9+setdata_change_q].STnTol),changedata,4);
				memcpy(&(tamp_buff[4]),changedata,4);
				dec2asciihex((REC_DATA.HC.Zone[9+setdata_change_q].STpTol),changedata,4);
				memcpy(&(tamp_buff[8]),changedata,4);
				memcpy(&HMIN_HMAX_send[1],tamp_buff,12);
				HMIN_HMAX_send[0] = 0x02; //START
				HMIN_HMAX_send[13] = 0x03; //END
				LRCresult = LRCcal(HMIN_HMAX_send,15);
				HMIN_HMAX_send[14] = LRCresult;
				if(send_dvf_tx_data(HMIN_HMAX_send,15)){
					temp_setmin_tempvalue[setdata_change_q] =REC_DATA.HC.Zone[9+setdata_change_q].STnTol;
					temp_setmax_tempvalue[setdata_change_q] = REC_DATA.HC.Zone[9+setdata_change_q].STpTol;
					setdata_change_q++;
				}else {
					setdata_change_delay_maxcount = 0;
				}
			}else {
				setdata_change_delay_maxcount = 0;
				setdata_change_q++;
			}
			if(setdata_change_q>=uv_MaxHotrunNum_tamp){
				//setdata_change_flag = 0;  //KBH160203
				setdata_change_step++;
				setdata_change_q = 0;
				setdata_change_delay_maxcount = 0;
			}
			break;
		case 3:  //KBH160203
			setdata_change_delay_count = 0;
			if ((MHC_temp_FPpass[setdata_change_q]!=REC_DATA.MHC_FPpass[setdata_change_q])&& (MA_FIX.OPT.MHC_Page_Enable == 2)){
				comdid = setdata_change_q/Node_Max_zone;
				slaveid = setdata_change_q-(comdid*Node_Max_zone); 	    	           
				dec2asciihex(comdid,changedata,1);
				tamp_buff[0] = changedata[0]; 
				if(REC_DATA.MHC_FPpass[setdata_change_q]){
					tamp_buff[1] = 0x83;
				}else{
					tamp_buff[1] = 0x84;
				}
				dec2asciihex(slaveid,changedata,2);
				memcpy(&(tamp_buff[2]),changedata,2);
				memcpy(&(used_send[1]),tamp_buff,4);
				LRCresult = LRCcal(used_send,7);
				used_send[6] = LRCresult;
				if(send_dvf_tx_data(used_send,7)){
					MHC_temp_FPpass[setdata_change_q]=REC_DATA.MHC_FPpass[setdata_change_q];
					setdata_change_q++;
				}else {
					setdata_change_delay_maxcount = 0;
				}
			}else {
				setdata_change_delay_maxcount = 0;
				setdata_change_q++;
			}
			if(setdata_change_q>=uv_MaxHotrunNum_tamp){
				setdata_change_flag = 0;
				setdata_change_step++;
				setdata_change_q = 0;
				setdata_change_delay_maxcount = 0;
			}
			break;
	}
}

//스타트 평션을 받았을때 처리 
void startupfuntion(char * data){
	int uv_MaxHotrunNum_tamp;		
	char send_set_packet[11];
	unsigned char LRCresult;
	unsigned char tamp_buff[8];					//171201 YJM char - > unsigned char
	int comdid; 
	int slaveid;
	int setvalue;
	int q;
	unsigned char changedata[4];   
	if(data[4]==0x03){
		hot_startupflag=1;
		
		if(MA_FIX.OPT.MHC_Page_Enable == 1){
			uv_MaxHotrunNum_tamp = (int)REC_DATA.MHC_CTRL.Zone_Set - 1 ;
		}else if(MA_FIX.OPT.MHC_Page_Enable == 2){ //160202 수정 
			uv_MaxHotrunNum_tamp = MA_FIX.OPT.SnExtTempZone;
		}
		switch (startupfuntion_step){
		
			case 0:
				if(MA_FIX.OPT.MHC_Page_Enable == 1){
					temp_set_tempvalue[startupfuntion_q]=REC_DATA.MHC[startupfuntion_q].SetValue;
				}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
					temp_set_tempvalue[startupfuntion_q]=REC_DATA.HC.Zone[9+startupfuntion_q].ST;
				}
				//comdid = startupfuntion_q/Node_Max_zone;	//160704 YJM
				comdid = 0;	//160704
				slaveid = startupfuntion_q-(comdid*Node_Max_zone);
  	          
				if(MA_FIX.OPT.MHC_Page_Enable == 1){
					setvalue = (REC_DATA.MHC[startupfuntion_q].SetValue*10)/setvalue_option;
				}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
					setvalue = REC_DATA.HC.Zone[9+startupfuntion_q].ST/setvalue_option;
				}
				dec2asciihex(comdid,changedata,1);
				tamp_buff[0] = changedata[0];
				tamp_buff[1] = 'B';
				dec2asciihex(slaveid,changedata,2);
				memcpy(&(tamp_buff[2]),changedata,2);
				dec2asciihex(setvalue,changedata,4);
				memcpy(&(tamp_buff[4]),changedata,4);
				memcpy(&send_set_packet[1],tamp_buff,8);
				send_set_packet[0] = 0x02; //START
				send_set_packet[9] = 0x03; //END
				LRCresult = LRCcal(send_set_packet,11);
				send_set_packet[10] = LRCresult;
				if(send_dvf_tx_data(send_set_packet,11)){
					startupfuntion_q++;
				}else {
					break;
				}
				if(startupfuntion_q>uv_MaxHotrunNum_tamp){
					startupfuntion_step++;
					startupfuntion_q = 0;
				}
				break;
			case 1:
				if(MA_FIX.OPT.MHC_Page_Enable == 1){
					temp_setmin_tempvalue[startupfuntion_q]=REC_DATA.MHC[startupfuntion_q].MinSet;
					temp_setmax_tempvalue[startupfuntion_q]=REC_DATA.MHC[startupfuntion_q].MaxSet;
				}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
					temp_setmin_tempvalue[startupfuntion_q]=REC_DATA.HC.Zone[9+startupfuntion_q].STnTol;
					temp_setmax_tempvalue[startupfuntion_q]=REC_DATA.HC.Zone[9+startupfuntion_q].STpTol;
				}
				//comdid = startupfuntion_q/Node_Max_zone;	//160704 YJM
				comdid = 0;		//160704
				slaveid = startupfuntion_q-(comdid*Node_Max_zone); 	    	           
				dec2asciihex(comdid,changedata,1);
				tamp_buff[0] = changedata[0];
				tamp_buff[1] = 0x80;
				dec2asciihex(slaveid,changedata,2);
				memcpy(&(tamp_buff[2]),changedata,2);
				if(MA_FIX.OPT.MHC_Page_Enable == 1){
					dec2asciihex((REC_DATA.MHC[startupfuntion_q].MinSet*10),changedata,4);
				}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
					dec2asciihex(REC_DATA.HC.Zone[9+startupfuntion_q].STnTol,changedata,4);
				}
				memcpy(&(tamp_buff[4]),changedata,4);
				if(MA_FIX.OPT.MHC_Page_Enable == 1){
					dec2asciihex((REC_DATA.MHC[startupfuntion_q].MaxSet*10),changedata,4);
				}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
					dec2asciihex(REC_DATA.HC.Zone[9+startupfuntion_q].STpTol,changedata,4);
				}
				memcpy(&(tamp_buff[8]),changedata,4);
				memcpy(&HMIN_HMAX_send[1],tamp_buff,12);
				HMIN_HMAX_send[0] = 0x02; //START
				HMIN_HMAX_send[13] = 0x03; //END
				LRCresult = LRCcal(HMIN_HMAX_send,15);
				HMIN_HMAX_send[14] = LRCresult;
				if(send_dvf_tx_data(HMIN_HMAX_send,15)){
					startupfuntion_q++;
				}else {
					break;
				}
				if(startupfuntion_q>uv_MaxHotrunNum_tamp){
					startupfuntion_step ++;
					startupfuntion_q = 0;
					//startupfuntion_flag = 0;
				}
				break;
			case 2:
				if(MA_FIX.OPT.MHC_Page_Enable == 1){
					temp_used_tempvalue[startupfuntion_q]=REC_DATA.MHC[startupfuntion_q].Enable;		//170313 YJM REC데이터로 변경
				}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
					temp_used_tempvalue[startupfuntion_q]=REC_DATA.HC.Zone[9+startupfuntion_q].SbOn;
				}
				//comdid = startupfuntion_q/Node_Max_zone;	//160704 YJM
				comdid = 0; 	//160704
				slaveid = startupfuntion_q-(comdid*Node_Max_zone); 	    	           
				dec2asciihex(comdid,changedata,1);
				tamp_buff[0] = changedata[0];
				if(MA_FIX.OPT.MHC_Page_Enable == 1){    
					if(REC_DATA.MHC[startupfuntion_q].Enable){		//170313 YJM REC데이터로 변경
						tamp_buff[1] = 0x81;
					}else{
						tamp_buff[1] = 0x82;
					}
				}else if(MA_FIX.OPT.MHC_Page_Enable == 2){
					if(REC_DATA.HC.Zone[9+startupfuntion_q].SbOn){
						tamp_buff[1] = 0x81;
					}else{
						tamp_buff[1] = 0x82;
					}
				}
				dec2asciihex(slaveid,changedata,2);
				memcpy(&(tamp_buff[2]),changedata,2);
				memcpy(&(used_send[1]),tamp_buff,4);
				LRCresult = LRCcal(used_send,7);
				used_send[6] = LRCresult;
				if(send_dvf_tx_data(used_send,7)){
					startupfuntion_q++;
				}else {
					break;
				}
				if(startupfuntion_q>uv_MaxHotrunNum_tamp){
					if(MA_FIX.OPT.MHC_Page_Enable == 2){ //KBH160203
						startupfuntion_step ++;
						startupfuntion_q = 0;
					}else{
						startupfuntion_step =0;
						startupfuntion_q = 0;
						startupfuntion_flag = 0;
					}
				}
				break;
			
			case 3:
				if(MA_FIX.OPT.MHC_Page_Enable == 2){ //KBH160203 FPpass 
					MHC_temp_FPpass[startupfuntion_q]=REC_DATA.MHC_FPpass[startupfuntion_q];
				
					comdid = startupfuntion_q/Node_Max_zone;
					slaveid = startupfuntion_q-(comdid*Node_Max_zone); 	    	           
					dec2asciihex(comdid,changedata,1);
					tamp_buff[0] = changedata[0];
					if(REC_DATA.MHC_FPpass[startupfuntion_q]){
						tamp_buff[1] = 0x83;
					}else{
						tamp_buff[1] = 0x84;
					}
					dec2asciihex(slaveid,changedata,2);
					memcpy(&(tamp_buff[2]),changedata,2);
					memcpy(&(used_send[1]),tamp_buff,4);
					LRCresult = LRCcal(used_send,7);
					used_send[6] = LRCresult;
					if(send_dvf_tx_data(used_send,7)){
						startupfuntion_q++;
					}else {
						break;
					}
					if(startupfuntion_q>uv_MaxHotrunNum_tamp){
						startupfuntion_step =0;
						startupfuntion_q = 0;
						startupfuntion_flag = 0;
					}
				}
				break;
		}	
	}  
}		
	
	


int asciihex2dec(unsigned char * data,int size){
	int result=0;
	unsigned char tmpdata[size];
	int q;
	memcpy(tmpdata,data,size);
	for(q=0;q<size;q++){
		if((tmpdata[q]>=0x41)&&(tmpdata[q]<=0x46))
			tmpdata[q]=tmpdata[q]-7;
	}
	if(size == 1){
		result = tmpdata[0]-0x30;
	}else if (size ==2){
		result = (tmpdata[0]-0x30)*16;
		result = result + (tmpdata[1]-0x30);
	}else if(size ==3){
		result = (tmpdata[0]-0x30)*256;
		result = result + (tmpdata[1]-0x30)*16;
		result = result + (tmpdata[2]-0x30);
	}else if(size ==4){
		result = (tmpdata[0]-0x30)*4096;
		result = result + (tmpdata[1]-0x30)*256;
		result = result + (tmpdata[2]-0x30)*16;
		result = result + (tmpdata[3]-0x30);
	}
	return result;
}

void dec2asciihex(unsigned int data,unsigned char * dsc,int size){
	unsigned char change_data[4];
	unsigned int tampdata;
	if(size == 1){
		dsc[0] = data;
		itoa(dsc[0],change_data);
		dsc[0] = change_data[0];
	}
	if(size == 2){
		dsc[0] = data/16;
		dsc[0] = dec2hex(dsc[0]);
		dsc[1] = data%16;
		dsc[1] = dec2hex(dsc[1]);
	}else if(size ==4){
		dsc[0] = data/4096;
		dsc[0] = dec2hex(dsc[0]);
		tampdata = data%4096;
		dsc[1] = tampdata/256;
		dsc[1] = dec2hex(dsc[1]);
		tampdata = tampdata%256;
		dsc[2] = tampdata/16;
		dsc[2] = dec2hex(dsc[2]);
		tampdata= tampdata%16;
		dsc[3] = tampdata;
		dsc[3] = dec2hex(dsc[3]);
	}
}
//10진수를 16 진수 아스키 코드로 변환 
unsigned char dec2hex (unsigned int data){
	if((data>=0)&&(data<10)){
		return data+0x30;
	}else if(data>=10){
		return data+0x37;
	}

}	

unsigned char LRCcal(char* data,int size){
	unsigned char reslut = 0;
	int q;
	for(q=1;q<size-2;q++){
		reslut = reslut + data[q];
	}
	if(MA_FIX.OPT.Sbchangsung_Enable == 1){
		reslut = (reslut|0x20)+0x50;
	}else {
		reslut = reslut|0x20;
	}
	
	return reslut;
}


UINT send_dvf_tx_data(char * data,int size){
	//	FRM_xopen(&Sender.FRM_xopen_0);			/*call the FRM_xopen() function*/
	if((MA_FIX.OPT.MHC_Page_Enable == 1)&& REC_DATA.MHC_CTRL.EnableCommunication == 0){	//170313 YJM REC데이터로 변경
		return 0;
	}
	Sender.FRM_gbuf_0.enable = 1;
	Sender.FRM_gbuf_0.ident  = Sender.FRM_xopen_0.ident;
			
	FRM_gbuf(&Sender.FRM_gbuf_0); 										/*call the FRM_gbuf() function*/
			
	Sender.buffer 			= (UDINT*)Sender.FRM_gbuf_0.buffer;			/* get adress of send buffer */
	Sender.buffer_length 	= size;//Sender.FRM_gbuf_0.buflng;					/* get length of send buffer */
	Sender.status			= Sender.FRM_gbuf_0.status;
			
	if(Sender.FRM_gbuf_0.status == 0){
		memset(Sender.FRM_gbuf_0.buffer, 0, Sender.FRM_gbuf_0.buflng);	/*clear sendbuffer*/
	}else {
		return 0;	
	}
	memcpy(Sender.buffer, data, size);//Sender.buffer_length);
	Sender.FRM_write_0.enable 	= 1;
	Sender.FRM_write_0.ident 	= Sender.FRM_xopen_0.ident;				/*ident from FRM_xopen()*/
	Sender.FRM_write_0.buffer 	= (UDINT)Sender.buffer;					/*sendbuffer*/
	Sender.FRM_write_0.buflng 	= size;							/*net length of senddata*/
	FRM_write(&Sender.FRM_write_0); 									/*call the FRM_write() function*/
	Sender.FRM_robuf_0.enable = 1;
	Sender.FRM_robuf_0.buffer = (UDINT)Sender.buffer;								/*sendbuffer*/
	Sender.FRM_robuf_0.buflng = Sender.buffer_length;					/*buffer length*/
		
	FRM_robuf(&Sender.FRM_robuf_0);										/*call the FRM_robuf() function*/
	return 1;
}
