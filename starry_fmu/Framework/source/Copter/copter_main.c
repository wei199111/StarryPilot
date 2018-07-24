/*
 * File      : copter_main.c
 *
 * Change Logs:
 * Date           Author       Notes
 * 2017-10-27     zoujiachi    first version.
 */
 
#include <rtthread.h>
#include "copter_main.h"
#include "att_estimator.h"
#include "console.h"
#include "delay.h"
#include "sensor_manager.h"
#include "control_main.h"
#include "filter.h"
#include "pos_estimator.h"
#include "hil_interface.h"
#include "param.h"
#include "gps.h"

#define EVENT_COPTER_FAST_LOOP		(1<<0)

static struct rt_timer timer_copter;
static struct rt_event event_copter;
uint32_t _att_est_period, _pos_est_period, _control_period;

static char* TAG = "Copter_Main";

void copter_main_loop(uint32_t att_est_period, uint32_t pos_est_period, uint32_t control_period)
{
	static uint32_t ahrs_time = 0;
	static uint32_t pos_est_time = 0;
	static uint32_t ctrl_time = 0;
	
	uint32_t now = time_nowMs();
	
#ifdef HIL_SIMULATION
	hil_collect_data();
#else
	sensor_collect();
#endif
	
	ctrl_att_adrc_update();
	
	if(TIME_GAP(ahrs_time, now) >= att_est_period){
		ahrs_time = now;
		attitude_est_run(0.001f*att_est_period);
	}
	
	if(TIME_GAP(pos_est_time, now) >= pos_est_period){
		pos_est_time = now;
		pos_est_update(0.001f*pos_est_period);
	}

	if(TIME_GAP(ctrl_time, now) >= control_period){
		ctrl_time = now;
		control_vehicle(0.001f*control_period);
	}
	
//	HomePosition home = ctrl_get_home();
//	struct vehicle_gps_position_s gps_t = gps_get_report();
//	Vector3f_t dis, dis2;
//	gps_calc_geometry_distance(&dis, home.lat, home.lon, (float)gps_t.lat*1e-7, (float)gps_t.lon*1e-7);
//	gps_calc_geometry_distance2(&dis2, home.lat, home.lon, (float)gps_t.lat*1e-7, (float)gps_t.lon*1e-7);
//	static uint32_t time = 0;
//	Console.print_eachtime(&time, 300, "x:%f y:%f x2:%f y2:%f sn:%d gps:%f %f eph:%f hdop:%f\n", dis.x, dis.y, dis2.x, dis2.y, gps_t.satellites_used,
//		(float)gps_t.lat*1e-7, (float)gps_t.lon*1e-7, gps_t.eph, gps_t.hdop);
}

static void timer_copter_update(void* parameter)
{
	rt_event_send(&event_copter, EVENT_COPTER_FAST_LOOP);
}

uint32_t copter_get_event_period(Copter_Period event)
{
	uint32_t period = 0;

	switch(event)
	{
		case AHRS_Period:
		{
			period = _att_est_period;
		}break;
		case Control_Period:
		{
			period = _control_period;
		}break;
		case Pos_Period:
		{	
			period = _pos_est_period;
		}break;
		default:
			break;
	}
	
	return period;
}

void copter_entry(void *parameter)
{
	rt_err_t res;
	rt_uint32_t recv_set = 0;
	rt_uint32_t wait_set = EVENT_COPTER_FAST_LOOP;
	
#ifdef HIL_SIMULATION
	_att_est_period = PARAM_GET_UINT32(HIL_SIM, HIL_ATT_EST_PERIOD);
	_pos_est_period = PARAM_GET_UINT32(HIL_SIM, HIL_POS_EST_PERIOD);
	_control_period = PARAM_GET_UINT32(HIL_SIM, HIL_CONTROL_PERIOD);
#else
	_att_est_period = AHRS_PERIOD;
	_pos_est_period = POS_EST_PERIOD;
	_control_period = CONTROL_PERIOD;
#endif	
	
	filter_init();
	attitude_est_init();
	control_init();
#ifdef HIL_SIMULATION
	hil_interface_init(HIL_SENSOR_LEVEL);
	Console.print("HIL Mode...\n");
#endif
	sensor_manager_init();
	pos_est_init(1e-3f*_pos_est_period);
	
	/* create event */
	res = rt_event_init(&event_copter, "copter_event", RT_IPC_FLAG_FIFO);

	/* register timer event */
	rt_timer_init(&timer_copter, "timer_copter",
					timer_copter_update,
					RT_NULL,
					1,
					RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
	rt_timer_start(&timer_copter);

	while(1)
	{
		res = rt_event_recv(&event_copter, wait_set, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 
								RT_WAITING_FOREVER, &recv_set);
		
		if(res == RT_EOK){
			if(recv_set & EVENT_COPTER_FAST_LOOP){
				copter_main_loop(_att_est_period, _pos_est_period, _control_period);
			}
		}
	}
}
