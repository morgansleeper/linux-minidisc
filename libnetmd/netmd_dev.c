/* netmd_dev.c
 *      Copyright (C) 2004, Bertrik Sikken
 *
 * This file is part of libnetmd.
 *
 * libnetmd is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libnetmd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include "libnetmd.h"
#include "netmd_dev.h"

#define NETMD_POLL_TIMEOUT	1000	/* miliseconds */
#define NETMD_SEND_TIMEOUT	1000
#define NETMD_RECV_TIMEOUT	1000
#define NETMD_RECV_TRIES		30

/*! list of known vendor/prod id's for NetMD devices */
static struct netmd_devices const known_devices[] =
{
	{0x54c, 0x75}, /* Sony MZ-N1 */
	{0x54c, 0x80}, /* Sony LAM-1 */
	{0x54c, 0x81}, /* Sony MDS-JB980 */
	{0x54c, 0x84}, /* Sony MZ-N505 */
	{0x54c, 0x85}, /* Sony MZ-S1 */
	{0x54c, 0x86}, /* Sony MZ-N707 */
	{0x54c, 0xc6}, /* Sony MZ-N10 */
	{0x54c, 0xc8}, /* Sony MZ-N710/N810 */
	{0x54c, 0xc9}, /* Sony MZ-N510/N610 */
	{0x54c, 0xca}, /* Sony MZ-NE410 */
	{0, 0} /* terminating pair */
};


/*
	polls to see if minidisc wants to send data

	IN	dev			  USB device handle
		  buf			  pointer to poll buffer
		  tries			maximum attempts to poll the minidisc

	Return value
	<0	if error
	>=0	number of bytes that md wants to send
*/
static int netmd_poll(usb_dev_handle *dev, unsigned char *buf, int tries)
{
	int i;

	for (i = 0; i < tries; i++) {
		/* send a poll message */
		memset(buf, 0, 4);
		if (usb_control_msg(dev, USB_ENDPOINT_IN | USB_TYPE_VENDOR |
												USB_RECIP_INTERFACE, 0x01, 0, 0, buf, 4,
												NETMD_POLL_TIMEOUT) < 0) {
			fprintf(stderr, "netmd_poll: usb_control_msg failed\n");
			return NETMDERR_USB;
		}
		if (buf[0] != 0) {
			break;
		}
		if (i > 0) {
			sleep(1);
		}
	}
	return buf[2];
}


/*
	exchanges a message with the minidisc

	IN	dev		USB device handle
			buf		pointer to buffer to send
			len		length of data to send

	Returns >0 on success, <0 on failure
*/
int netmd_exch_message(netmd_dev_handle *devh, unsigned char *cmd, int cmdlen,
	unsigned char *rsp)
{
	unsigned char	pollbuf[4];
	int		len;
	usb_dev_handle	*dev;

	dev = (usb_dev_handle *)devh;
	
	/* poll to see if we can send data */
	len = netmd_poll(dev, pollbuf, 1);
	if (len != 0) {
		fprintf(stderr, "netmd_exch_message: netmd_poll failed\n");
		return (len > 0) ? NETMDERR_NOTREADY : len;
	}

	/* send data */
	netmd_trace(NETMD_TRACE_INFO, "Command:\n");
	netmd_trace_hex(NETMD_TRACE_INFO, cmd, cmdlen);
	if (usb_control_msg(dev, USB_ENDPOINT_OUT | USB_TYPE_VENDOR |
			 								USB_RECIP_INTERFACE, 0x80, 0, 0, cmd, cmdlen,
			 								NETMD_SEND_TIMEOUT) < 0) {
		fprintf(stderr, "netmd_exch_message: usb_control_msg failed\n");
		return NETMDERR_USB;
	}

	do {
		/* poll for data that minidisc wants to send */
		len = netmd_poll(dev, pollbuf, NETMD_RECV_TRIES);
		if (len <= 0) {
			fprintf(stderr, "netmd_exch_message: netmd_poll failed\n");
			return (len == 0) ? NETMDERR_TIMEOUT : len;
		}

		/* receive data */
		if (usb_control_msg(dev, USB_ENDPOINT_IN | USB_TYPE_VENDOR |
											 	USB_RECIP_INTERFACE, pollbuf[1], 0, 0, rsp, len,
											 	NETMD_RECV_TIMEOUT) < 0) {
			fprintf(stderr, "netmd_exch_message: usb_control_msg failed\n");
			return NETMDERR_USB;
		}
		netmd_trace(NETMD_TRACE_INFO, "Response:\n");
		netmd_trace_hex(NETMD_TRACE_INFO, rsp, len);
		/* get response again if player responds with 0x0F.	*/
	} while (rsp[0] == 0x0F);

	/* return length */
	return len;
}


int netmd_init(netmd_device_t **device_list)
{
	struct usb_bus *bus;
	struct usb_device *dev;
	int count = 0;
	int num_devices;
	netmd_device_t	*new_device;

	usb_init();

	usb_find_busses();
	usb_find_devices();

	*device_list = NULL;
	num_devices = 0;
	for(bus = usb_get_busses(); bus; bus = bus->next)
	{
		for(dev = bus->devices; dev; dev = dev->next)
		{
			for(count = 0; (known_devices[count].idVendor != 0); count++)
			{
				if(dev->descriptor.idVendor == known_devices[count].idVendor
				   && dev->descriptor.idProduct == known_devices[count].idProduct)
				{
					/* TODO: check if linked list stuff is really correct */
					new_device = malloc(sizeof(netmd_device_t));
					new_device->usb_dev = dev;
					new_device->link = *device_list;
					*device_list = new_device;
					num_devices++;
				}
			}
		}
	}

	return num_devices;
}


netmd_dev_handle* netmd_open(netmd_device_t *netmd_dev)
{
	usb_dev_handle* dh;
	
	dh = usb_open(netmd_dev->usb_dev);
	usb_claim_interface(dh, 0);

	return (netmd_dev_handle *)dh;
}


int netmd_get_devname(netmd_dev_handle* devh, unsigned char* buf, int buffsize)
{
	usb_dev_handle *dev;
	
	dev = (usb_dev_handle *)devh;
	if (usb_get_string_simple(dev, 2, buf, buffsize) < 0) {
		fprintf(stderr, "usb_get_string_simple failed, %s (%d)\n", strerror(errno), errno);
		buf[0] = 0;
		return 0;
	}

	return strlen(buf);
}


void netmd_close(netmd_dev_handle* devh)
{
	usb_dev_handle *dev;
	
	dev = (usb_dev_handle *)devh;
	usb_release_interface(dev, 0);
	usb_close(dev);
}


void netmd_clean(netmd_device_t *device_list)
{
	netmd_device_t *tmp, *device;

	device = device_list;
	while (device != NULL) {
		tmp = device->link;
		free(device);
		device = tmp;
	}
}

