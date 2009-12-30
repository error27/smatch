/*
 * sparse/check_dma_on_stack.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

struct {
	const char *name;
	int param;
} dma_funcs[] = {
	{"acm_ctrl_msg", 3},
	{"aiptek_get_report", 3},
	{"aiptek_set_report", 3},
	{"alauda_get_media_signatures", 1},
	{"alauda_get_media_status", 1},
	{"ark3116_read_reg", 2},
	{"at76_dfu_get_state", 1},
	{"at76_dfu_get_status", 1},
	{"at76_load_int_fw_block", 2},
	{"BoxGetRegister", 3},
	{"catc_ctrl_msg", 5},
	{"ch341_control_in", 4},
	{"cpia2_usb_transfer_cmd", 1},
	{"cpia_usb_transferCmd", 2},
	{"dib0700_ctrl_rd", 3},
	{"dib0700_ctrl_wr", 1},
	{"dtv5100_i2c_msg", 4},
	{"flexcop_usb_i2c_req", 5},
	{"flexcop_usb_i2c_request", 4},
	{"flexcop_usb_readwrite_dw", 2},
	{"flexcop_usb_v8_memory_req", 4},
	{"get_descriptor_addr", 2},
	{"get_hub_descriptor", 1},
	{"get_hub_status", 1},
	{"get_manuf_info", 1},
	{"get_port_status", 2},
	{"get_registers", 3},
	{"gl861_i2c_msg", 4},
	{"go7007_usb_vendor_request", 4},
	{"gp8psk_usb_in_op", 4},
	{"gp8psk_usb_out_op", 4},
	{"hid_get_class_descriptor", 3},
	{"__hwahc_op_mmcie_add", 4},
	{"hwarc_cmd", 1},
	{"klsi_105_chg_port_settings", 1},
	{"konicawc_ctrl_msg", 5},
	{"line6_read_data", 2},
	{"line6_read_serial_number", 1},
	{"line6_write_data", 2},
	{"mcs_get_reg", 2},
	{"mct_u232_get_modem_stat", 1},
	{"mos7840_get_reg_sync", 2},
	{"mos7840_get_uart_reg", 2},
	{"nc_vendor_read", 3},
	{"PIPEnsControlOutAsyn", 5},
	{"pl2303_vendor_read", 3},
	{"qcm_stv_getw", 2},
	{"qt2_box_get_register", 3},
	{"qt2_openboxchannel", 2},
	{"qt_open_channel", 2},
	{"read_download_mem", 4},
	{"read_packet", 2},
	{"ReadPacket", 2},
	{"read_ram", 3},
	{"read_rom", 3},
	{"recv_control_msg", 4},
	{"rndis_command", 1},
	{"__rpipe_get_descr", 1},
	{"__rpipe_set_descr", 1},
	{"rt2x00usb_vendor_request", 5},
	{"s2255_vendor_req", 4},
	{"se401_sndctrl", 4},
	{"send_cmd", 4},
	{"__send_control_msg", 4},
	{"send_control_msg", 4},
	{"set_registers", 3},
	{"si470x_get_report", 1},
	{"si470x_set_report", 1},
	{"sierra_get_swoc_info", 1},
	{"stk_camera_read_reg", 2},
	{"stv_sndctrl", 4},
	{"ti_command_in_sync", 4},
	{"ti_command_out_sync", 4},
	{"ti_vread_sync", 4},
	{"ti_vsend_sync", 4},
	{"usb_control_msg", 6},
	{"usb_cypress_writemem", 2},
	{"usbduxfastsub_upload", 1},
	{"usbduxsub_upload", 1},
	{"usb_get_descriptor", 3},
	{"usb_get_langid", 1},
	{"usb_get_report", 3},
	{"usb_get_report", 4},
	{"usb_get_string", 3},
	{"usblp_ctrl_msg", 6},
	{"usb_read", 4},
	{"usb_set_report", 3},
	{"usb_string_sub", 3},
	{"usb_write", 4},
	{"__uvc_query_ctrl", 5},
	{"uvc_query_ctrl", 5},
	{"vendor_command", 4},
	{"vp702x_usb_in_op", 4},
	{"vp702x_usb_inout_op", 1},
	{"vp702x_usb_inout_op", 3},
	{"vp702x_usb_out_op", 4},
	{"w9968cf_write_fsb", 1},
	{"write_i2c_mem", 4},
	{"write_packet", 2},
	{"WritePacket", 2},
	{"write_rom", 3},
	{"yealink_cmd", 1},
};

static void match_dma_func(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;
	struct symbol *sym;
	char *name;

	arg = get_argument_from_call_expr(expr->args, (int)param);
	arg = strip_expr(arg);
	if (!arg)
		return;
	if (arg->type == EXPR_PREOP && arg->op == '&') {
		if (arg->unop->type != EXPR_SYMBOL)
			return;
		name = get_variable_from_expr(arg, NULL);
		sm_msg("error: doing dma on the stack (%s)", name);
		free_string(name);
		return;
	}
	if (arg->type != EXPR_SYMBOL)
		return;
	sym = get_type(arg);
	if (!sym || sym->type != SYM_ARRAY)
		return;
	name = get_variable_from_expr(arg, NULL);
	sm_msg("error: doing dma on the stack (%s)", name);
	free_string(name);
}

void check_dma_on_stack(int id)
{
	int i;

	if (option_project != PROJ_KERNEL)
		return;
	my_id = id;
	for (i = 0; i < ARRAY_SIZE(dma_funcs); i++) {
		add_function_hook(dma_funcs[i].name, &match_dma_func, 
				(void *)dma_funcs[i].param);
	}
}
