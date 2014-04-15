/**
 * Copyright © 2014  Mattias Andrée (maandree@member.fsf.org)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "fake_quartz.h"


#include <stdio.h>
#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/randr.h>


/* This file very sloppily translates Mac OS X Quartz calls to X RandR calls.
   It should by no means be used, without additional modification, as a
   part of a compatibility layer. The purpose of this file is only to make
   it possible to test for logical errors in Max OS X specific code on
   a GNU/Linux system under X. */


static xcb_connection_t* conn = NULL;
static xcb_randr_get_screen_resources_current_reply_t* res_reply = NULL;
static uint32_t crtc_count = 0;
static xcb_randr_crtc_t* crtcs = NULL;
static uint16_t* original_ramps = NULL;


CGError CGGetOnlineDisplayList(uint32_t max_size, CGDirectDisplayID* displays_out, uint32_t* count_out)
{
  uint32_t i;
  
  if (conn == NULL)
    {
      xcb_generic_error_t* error;
      xcb_screen_iterator_t iter;
      xcb_randr_get_screen_resources_current_cookie_t res_cookie;
      xcb_randr_get_crtc_gamma_cookie_t gamma_cookie;
      xcb_randr_get_crtc_gamma_reply_t* gamma_reply;
      
      conn = xcb_connect(NULL, NULL);
      
      iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
      res_cookie = xcb_randr_get_screen_resources_current(conn, iter.data->root);
      res_reply = xcb_randr_get_screen_resources_current_reply(conn, res_cookie, &error);
      
      if (error)
	{
	  fprintf(stderr, "Failed to open X connection\n");
	  xcb_disconnect(conn);
	  crtc_count = 0;
	  return ~kCGErrorSuccess;
	}
      
      crtc_count = (uint32_t)(res_reply->num_crtcs);
      crtcs = xcb_randr_get_screen_resources_current_crtcs(res_reply);
      
      original_ramps = malloc(crtc_count * 3 * 256 * sizeof(uint16_t));
      if (original_ramps == NULL)
	{
	  perror("malloc");
	  xcb_disconnect(conn);
	  crtc_count = 0;
	  return ~kCGErrorSuccess;
	}
      
      for (i = 0; i < crtc_count; i++)
	{
	  gamma_cookie = xcb_randr_get_crtc_gamma(conn, crtcs[i]);
	  gamma_reply = xcb_randr_get_crtc_gamma_reply(conn, gamma_cookie, &error);
	  
	  if (error)
	    {
	      fprintf(stderr, "Failed to read gamma ramps\n");
	      xcb_disconnect(conn);
	      crtc_count = 0;
	      return ~kCGErrorSuccess;
	    }
	  
	  memcpy(original_ramps + (0 + 3 * i) * 256, xcb_randr_get_crtc_gamma_red(gamma_reply), 256);
	  memcpy(original_ramps + (1 + 3 * i) * 256, xcb_randr_get_crtc_gamma_green(gamma_reply), 256);
	  memcpy(original_ramps + (2 + 3 * i) * 256, xcb_randr_get_crtc_gamma_blue(gamma_reply), 256);
	  
	  free(gamma_reply);
	}
    }
  
  for (i = 0; (i < max_size) && (i < crtc_count); i++)
    *(displays_out + i) = (CGDirectDisplayID)i;
  
  *count_out = i;
  return kCGErrorSuccess;
}


CGError CGSetDisplayTransferByTable(CGDirectDisplayID display, uint32_t gamma_size, const CGGammaValue* red,
				    const CGGammaValue* green, const CGGammaValue* blue)
{
  xcb_void_cookie_t gamma_cookie;
  uint16_t r_int[256];
  uint16_t g_int[256];
  uint16_t b_int[256];
  long i;
  int32_t v;
  
  if (gamma_size != 256)
    {
      fprintf(stderr, "Gamma size should be 256\n");
      abort();
    }
  
  for (i = 0; i < 256; i++)
    {
      v = red[i] * (UINT16_MAX - 1);
      r_int[i] = v < 0 ? 0 : v >= UINT16_MAX : (UINT16_MAX - 1) : v;
      
      v = green[i] * (UINT16_MAX - 1);
      g_int[i] = v < 0 ? 0 : v >= UINT16_MAX : (UINT16_MAX - 1) : v;
      
      v = blue[i] * (UINT16_MAX - 1);
      b_int[i] = v < 0 ? 0 : v >= UINT16_MAX : (UINT16_MAX - 1) : v;
    }
  
  gamma_cookie = xcb_randr_set_crtc_gamma_checked(conn, crtcs[display], gamma_size, r_int, g_int, b_int);
  return xcb_request_check(conn, gamma_cookie) == NULL ? kCGErrorSuccess : ~kCGErrorSuccess;
}


CGError CGGetDisplayTransferByTable(CGDirectDisplayID display, uint32_t gamma_size, CGGammaValue* red,
				    CGGammaValue* green, CGGammaValue* blue, uint32_t *gamma_size_out)
{
  xcb_randr_get_crtc_gamma_cookie_t gamma_cookie;
  xcb_randr_get_crtc_gamma_reply_t* gamma_reply;
  xcb_generic_error_t* error;
  uint16_t* r_int;
  uint16_t* g_int;
  uint16_t* b_int;
  
  if (gamma_size != 256)
    {
      fprintf(stderr, "Gamma size should be 256\n");
      abort();
    }
  
  *gamma_size_out = 256;
  
  gamma_cookie = xcb_randr_get_crtc_gamma(conn, crtcs[i]);
  gamma_reply = xcb_randr_get_crtc_gamma_reply(conn, gamma_cookie, &error);
  
  if (error)
    {
      fprintf(stderr, "Failed to write gamma ramps\n");
      return ~kCGErrorSuccess;
    }
  
  r_int = xcb_randr_get_crtc_gamma_red(gamma_reply);
  g_int = xcb_randr_get_crtc_gamma_green(gamma_reply);
  b_int = xcb_randr_get_crtc_gamma_blue(gamma_reply);
  
  for (i = 0; i < 256; i++)
    {
      red[i]   = (CGGammaValue)(r_int[i]) / (UINT16_MAX - 1);
      green[i] = (CGGammaValue)(g_int[i]) / (UINT16_MAX - 1);
      blue[i]  = (CGGammaValue)(b_int[i]) / (UINT16_MAX - 1);
    }
  
  return kCGErrorSuccess;
}


void CGDisplayRestoreColorSyncSettings(void)
{
  uint32_t i;
  for (i = 0; i < crtc_count; i++)
    {
      xcb_randr_set_crtc_gamma(conn, crtcs[i], 255,
			       original_ramps + (0 + 3 * i) * 256,
			       original_ramps + (1 + 3 * i) * 256,
			       original_ramps + (2 + 3 * i) * 256);
    }
}


uint32_t CGDisplayGammaTableCapacity(CGDirectDisplayID display)
{
  return 256;
}


void close_fake_quartz(void)
{
  if (res_reply != NULL)
    {
      free(res_reply);
      res_reply = NULL;
    }
  if (conn != NULL)
    {
      xcb_disconnect(conn);
      conn = NULL;
    }
  if (original_ramps != NULL)
    {
      free(original_ramps);
      original_ramps = NULL;
    }
}
