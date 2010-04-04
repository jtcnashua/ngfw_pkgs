/* bnx2i.c: Broadcom NetXtreme II iSCSI driver.
 *
 * Copyright (c) 2006 - 2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Anil Veerabhadrappa (anilgv@broadcom.com)
 */

#include "bnx2i.h"
#ifdef __VMKLNX__
#include <vmklinux26/vmklinux26_scsi.h>
#include <scsi/scsi_transport.h>
#endif

static struct list_head adapter_list;
static u32 adapter_count;
int bnx2i_reg_device = 0;

#define DRV_MODULE_NAME		"bnx2i"
#define DRV_MODULE_VERSION	"1.8.12f"
#define DRV_MODULE_RELDATE	"Jan 19, 2010"

static char version[] __devinitdata =
		"Broadcom NetXtreme II iSCSI Driver " DRV_MODULE_NAME \
		" v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";


MODULE_AUTHOR("Anil Veerabhadrappa <anilgv@broadcom.com>");
MODULE_DESCRIPTION("Broadcom NetXtreme II BCM5706/5708/5709/57710 iSCSI Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

static DEFINE_MUTEX(bnx2i_dev_lock);

unsigned int event_coal_div = 1;
module_param(event_coal_div, int, 0664);
MODULE_PARM_DESC(event_coal_div, "Event Coalescing Divide Factor");

unsigned int bnx2i_nopout_when_cmds_active = 1;
module_param(bnx2i_nopout_when_cmds_active, int, 0664);
MODULE_PARM_DESC(bnx2i_nopout_when_cmds_active,
		"iSCSI NOOP even when connection is not idle");

unsigned int en_tcp_dack = 1;
module_param(en_tcp_dack, int, 0664);
MODULE_PARM_DESC(en_tcp_dack, "Enable TCP Delayed ACK");

unsigned int time_stamps = 0;
module_param(time_stamps, int, 0664);
MODULE_PARM_DESC(time_stamps, "Enable TCP TimeStamps");

unsigned int error_mask1 = 0x00;
module_param(error_mask1, int, 0664);
MODULE_PARM_DESC(error_mask1, "Config FW iSCSI Error Mask #1");

unsigned int error_mask2 = 0x00;
module_param(error_mask2, int, 0664);
MODULE_PARM_DESC(error_mask2, "Config FW iSCSI Error Mask #2");

unsigned int sq_size = 0;
module_param(sq_size, int, 0664);
MODULE_PARM_DESC(sq_size, "Configure SQ size");

unsigned int rq_size = BNX2I_RQ_WQES_DEFAULT;
module_param(rq_size, int, 0664);
MODULE_PARM_DESC(rq_size, "Configure RQ size");

unsigned int tcp_buf_size = 65536;
module_param(tcp_buf_size, int, 0664);
MODULE_PARM_DESC(tcp_buf_size, "TCP send/receive buffer size");

unsigned int event_coal_min = 24;
module_param(event_coal_min, int, 0664);
MODULE_PARM_DESC(event_coal_min, "Event Coalescing Minimum Commands");

unsigned int cmd_cmpl_per_work = 24;
module_param(cmd_cmpl_per_work, int, 0664);
MODULE_PARM_DESC(cmd_cmpl_per_work, "Number of CQE's processed per work");

u64 iscsi_error_mask = 0x00;

extern struct cnic_ulp_ops bnx2i_cnic_cb;
extern spinlock_t bnx2i_resc_lock; /* protects global data structures */
extern struct tcp_port_mngt bnx2i_tcp_port_tbl;

static void bnx2i_unreg_one_device(struct bnx2i_hba *hba) ;
#ifndef __VMKLNX__
static int bnx2i_bind_adapter_devices(struct bnx2i_hba *hba);
static void bnx2i_unbind_adapter_devices(struct bnx2i_hba *hba);
#else
void bnx2i_unbind_adapter_devices(struct bnx2i_hba *hba);
#endif


static void bnx2i_unreg_unsupported_devices(void)
{
	struct bnx2i_hba *hba = NULL;
	struct bnx2i_hba *temp;
	int iscsi_sup;

	if (!adapter_count)
		return;

	mutex_lock(&bnx2i_dev_lock);
	list_for_each_entry_safe(hba, temp, &adapter_list, link) {
		iscsi_sup = 1;
		bnx2i_register_device(hba, BNX2I_REGISTER_HBA_FORCED);
		if (test_bit(CNIC_F_IF_UP, &hba->cnic->flags) &&
		    !hba->cnic->max_iscsi_conn) {
			printk(KERN_ALERT "bnx2i: dev %s does not support "
					  "iscsi\n", hba->netdev->name);
			iscsi_sup = 0;
		}
		bnx2i_unreg_one_device(hba);
		if (!iscsi_sup)
			bnx2i_deregister_xport(hba);
	}
	mutex_unlock(&bnx2i_dev_lock);
}

/**
 * bnx2i_identify_device - identifies NetXtreme II device type
 *
 * @hba: 		Adapter structure pointer
 *
 * This function identifies the NX2 device type and sets appropriate
 *	queue mailbox register access method, 5709 requires driver to
 *	access MBOX regs using *bin* mode
 **/
void bnx2i_identify_device(struct bnx2i_hba *hba)
{
	hba->cnic_dev_type = 0;
	if ((hba->pci_did == PCI_DEVICE_ID_NX2_5706) ||
	    (hba->pci_did == PCI_DEVICE_ID_NX2_5706S))
		set_bit(BNX2I_NX2_DEV_5706, &hba->cnic_dev_type);
	else if ((hba->pci_did == PCI_DEVICE_ID_NX2_5708) ||
	    (hba->pci_did == PCI_DEVICE_ID_NX2_5708S))
		set_bit(BNX2I_NX2_DEV_5708, &hba->cnic_dev_type);
	else if ((hba->pci_did == PCI_DEVICE_ID_NX2_5709) ||
	    (hba->pci_did == PCI_DEVICE_ID_NX2_5709S)) {
		set_bit(BNX2I_NX2_DEV_5709, &hba->cnic_dev_type);
		hba->mail_queue_access = 0| BNX2I_MQ_BIN_MODE;
	} else if (hba->pci_did == PCI_DEVICE_ID_NX2_57710 ||
		   hba->pci_did == PCI_DEVICE_ID_NX2_57711 ||
		   hba->pci_did == PCI_DEVICE_ID_NX2_57711E)
		set_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type);
	else
		printk(KERN_ERR "bnx2i- unknown device, 0x%x\n", hba->pci_did);
}

/**
 * get_adapter_list_head - returns head of adapter list
 *
 **/
struct bnx2i_hba *get_adapter_list_head(void)
{
	struct bnx2i_hba *hba = NULL;
	struct bnx2i_hba *tmp_hba;

	if (!adapter_count)
		goto hba_not_found;

	mutex_lock(&bnx2i_dev_lock);
	list_for_each_entry(tmp_hba, &adapter_list, link) {
		if (tmp_hba->cnic && tmp_hba->cnic->cm_select_dev) {
			hba = tmp_hba;
			break;
		}
	}
	mutex_unlock(&bnx2i_dev_lock);
hba_not_found:
	return hba;
}

/**
 * bnx2i_get_hba_from_template - maps scsi_transport_template to
 *		bnx2i adapter pointer
 * @scsit: 		scsi transport template pointer
 *
 **/
struct bnx2i_hba *bnx2i_get_hba_from_template(
			struct scsi_transport_template *scsit)
{
	struct list_head *list;
	struct bnx2i_hba *hba = NULL;
	int match_found = 0;

	mutex_lock(&bnx2i_dev_lock);
	list_for_each(list, &adapter_list) {
		hba = (struct bnx2i_hba *) list;
		if (hba->shost_template == scsit) {
			match_found = 1;
			break;
		}
	}
	mutex_unlock(&bnx2i_dev_lock);

	if (match_found == 0)
		hba = NULL;
	return hba;
}


/**
 * bnx2i_find_hba_for_cnic - maps cnic device instance to bnx2i adapter instance
 *
 * @cnic: 		pointer to cnic device instance
 *
 **/
struct bnx2i_hba *bnx2i_find_hba_for_cnic(struct cnic_dev *cnic)
{
	struct bnx2i_hba *hba, *temp;

	mutex_lock(&bnx2i_dev_lock);
	list_for_each_entry_safe(hba, temp, &adapter_list, link) {
		if (hba->cnic == cnic) {
			mutex_unlock(&bnx2i_dev_lock);
			return hba;
		}
	}
	mutex_unlock(&bnx2i_dev_lock);
	return NULL;
}


#ifdef __VMKLNX__
static void bnx2i_check_iscsi_support(struct bnx2i_hba *hba)
{
	if (!hba->shost_template)
		return;

	/* unless you register the device cnic can't get information
	 * to determine iscsi is supported or not
	 */
	bnx2i_register_device(hba, BNX2I_REGISTER_HBA_FORCED);
	bnx2i_register_xport(hba);
	bnx2i_unreg_one_device(hba);
}

void bnx2i_lookup_iscsi_hba_vm(void)
{
	struct list_head *list;
	struct bnx2i_hba *hba = NULL;

	if (!adapter_count)
		return;

	list_for_each(list, &adapter_list) {
		hba = (struct bnx2i_hba *) list;

		bnx2i_check_iscsi_support(hba);
	}
}
#endif	/*  __VMKLNX__ */

/**
 * bnx2i_start - cnic callback to initialize & start adapter instance
 *
 * @handle: 		transparent handle pointing to adapter structure
 *
 * This function maps adapter structure to pcidev structure and initiates
 *	firmware handshake to enable/initialize on chip iscsi components
 * 	This bnx2i - cnic interface api callback is issued after following
 *	2 conditions are met -
 *	  a) underlying network interface is up (marked by event 'NETDEV_UP'
 *		from netdev
 *	  b) bnx2i adapter instance is registered
 **/
void bnx2i_start(void *handle)
{
#define BNX2I_INIT_POLL_TIME	(1000 / HZ)
	struct bnx2i_hba *hba = handle;
	int i = HZ;

#ifndef __VMKLNX__
	bnx2i_bind_adapter_devices(hba);
#endif

	/* For interfaces/devices already initialized (ifup) register
	 * the corresponding transport name if it support iSCSI offload.
	 * For devices brought up after 'bnx2i' loads, transport name
	 * will be registered in netdev event callback.
	 */
	if (!hba->shost_template)
		bnx2i_register_xport(hba);

	bnx2i_send_fw_iscsi_init_msg(hba);
	do {
		if (test_bit(ADAPTER_STATE_UP, &hba->adapter_state) ||
		    test_bit(ADAPTER_STATE_INIT_FAILED, &hba->adapter_state))
			break;
		msleep(BNX2I_INIT_POLL_TIME);
	} while (i--);
}


/**
 * bnx2i_stop - cnic callback to shutdown adapter instance
 *
 * @handle: 		transparent handle pointing to adapter structure
 *
 * driver checks if adapter is already in shutdown mode, if not start
 *	the shutdown process
 **/
void bnx2i_stop(void *handle)
{
	struct bnx2i_hba *hba = handle;

	/* check if cleanup happened in GOING_DOWN context */
	if (!test_bit(ADAPTER_STATE_GOING_DOWN, &hba->adapter_state)) {
		mutex_lock(&hba->net_dev_lock);
		set_bit(ADAPTER_STATE_GOING_DOWN, &hba->adapter_state);
		mutex_unlock(&hba->net_dev_lock);
		bnx2i_start_iscsi_hba_shutdown(hba);
	}
	clear_bit(ADAPTER_STATE_GOING_DOWN, &hba->adapter_state);
	clear_bit(ADAPTER_STATE_UP, &hba->adapter_state);
	
}

/**
 * bnx2i_register_device - register bnx2i adapter instance with the cnic driver
 *
 * @hba: 		Adapter instance to register
 * @reg_mode:           0 -> register only licensed hba, 1 -> all
 *
 * registers bnx2i adapter instance with the cnic driver while holding the
 *	adapter structure lock
 **/
void bnx2i_register_device(struct bnx2i_hba *hba, int reg_mode)
{
	if (test_bit(ADAPTER_STATE_GOING_DOWN, &hba->adapter_state) ||
	    test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic) ||
	    ((reg_mode == BNX2I_REGISTER_HBA_SUPPORTED) && 
	     (!hba->cnic->max_iscsi_conn)))
		return;

	hba->cnic->register_device(hba->cnic, CNIC_ULP_ISCSI, (void *) hba);

	bnx2i_reg_device++;
	set_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
}


/**
 * bnx2i_reg_dev_all - registers all bnx2i adapter instances with the
 *			cnic driver
 *
 * registers all bnx2i adapter instances with the cnic driver while holding
 *	the global resource lock
 **/
void bnx2i_reg_dev_all(void)
{
	struct bnx2i_hba *hba, *temp;

	mutex_lock(&bnx2i_dev_lock);
	list_for_each_entry_safe(hba, temp, &adapter_list, link) {
#ifdef __VMKLNX__
		bnx2i_register_device(hba, BNX2I_REGISTER_HBA_SUPPORTED);
#else
		bnx2i_register_device(hba, BNX2I_REGISTER_HBA_FORCED);
#endif
	}
	mutex_unlock(&bnx2i_dev_lock);
}


/**
 * bnx2i_unreg_one_device - unregister bnx2i adapter instance with
 *			the cnic driver
 *
 * @hba: 		Adapter instance to unregister
 *
 * registers bnx2i adapter instance with the cnic driver while holding
 *	the adapter structure lock
 **/
static void bnx2i_unreg_one_device(struct bnx2i_hba *hba) 
{
	if (!test_bit(CNIC_F_IF_UP, &hba->cnic->flags) ||
	    !test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic) ||
	    test_bit(ADAPTER_STATE_GOING_DOWN, &hba->adapter_state) ||
	    hba->ofld_conns_active)
		return;

	hba->cnic->unregister_device(hba->cnic, CNIC_ULP_ISCSI);
	barrier();
	bnx2i_reg_device--;
	/* ep_disconnect could come before NETDEV_DOWN, driver won't
	 * see NETDEV_DOWN as it already unregistered itself.
	 */
	hba->adapter_state = 0;
	clear_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
#ifndef __VMKLNX__
	bnx2i_unbind_adapter_devices(hba);
#endif
}

/**
 * bnx2i_unreg_dev_all - unregisters all bnx2i adapter instances with the
 *			cnic driver
 *
 * unregisters all bnx2i adapter instances with the cnic driver while holding
 *	the global resource lock
 **/
void bnx2i_unreg_dev_all(void)
{
	struct bnx2i_hba *hba, *temp;

	mutex_lock(&bnx2i_dev_lock);
	list_for_each_entry_safe(hba, temp, &adapter_list, link)
		bnx2i_unreg_one_device(hba);
	mutex_unlock(&bnx2i_dev_lock);
}


/**
 * bnx2i_bind_adapter_devices - binds bnx2i adapter with the associated
 *			pcidev structure
 *
 * @hba: 		Adapter instance
 *
 * With newly introduced changes to bnx2i - cnic interface, cnic_dev's 'pcidev'
 *	field will be valid only after bnx2i registers the device instance.
 *	Earlier this field was valid right during device enumeration process 
 **/
#ifndef __VMKLNX__
static 
#endif
int bnx2i_bind_adapter_devices(struct bnx2i_hba *hba)
{
	struct cnic_dev *cnic;
	int nx2_10g_dev = 0;

	if (!hba->cnic)
		return -ENODEV;

	cnic = hba->cnic;
	hba->pcidev = cnic->pcidev;
	if (hba->pcidev) {
		pci_dev_get(hba->pcidev);
		hba->pci_did = hba->pcidev->device;
		hba->pci_vid = hba->pcidev->vendor;
		hba->pci_sdid = hba->pcidev->subsystem_device;
		hba->pci_svid = hba->pcidev->subsystem_vendor;
		hba->pci_func = PCI_FUNC(hba->pcidev->devfn);
		hba->pci_devno = PCI_SLOT(hba->pcidev->devfn);
	}

	bnx2i_identify_device(hba);

	if (test_bit(BNX2I_NX2_DEV_5709, &hba->cnic_dev_type)) {
		hba->regview = ioremap_nocache(hba->netdev->base_addr,
					       BNX2_MQ_CONFIG2);
		if (!hba->regview)
			goto mem_err;
	} else if (test_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type)) {
		nx2_10g_dev = 1;
		hba->regview = ioremap_nocache(hba->netdev->base_addr, 4096);
		if (!hba->regview)
			goto mem_err;
	}

	if (nx2_10g_dev) {
		hba->conn_teardown_tmo = 15 * HZ;
		hba->conn_ctx_destroy_tmo = 6 * HZ;
		hba->hba_shutdown_tmo = 240 * HZ;
	} else {
		hba->conn_teardown_tmo = 6 * HZ;
		hba->conn_ctx_destroy_tmo = 2 * HZ;
		hba->hba_shutdown_tmo = 30 * HZ;
	}

	if (bnx2i_setup_mp_bdt(hba))
		goto mem_err;

	return 0;

mem_err:
	bnx2i_unbind_adapter_devices(hba);
	return -ENOMEM;
}


/**
 * bnx2i_unbind_adapter_devices - removes bnx2i adapter to pcidev mapping
 *
 * @hba: 		Adapter instance
 *
 **/
void bnx2i_unbind_adapter_devices(struct bnx2i_hba *hba)
{
	if (hba->regview) {
		iounmap(hba->regview);
		hba->regview = NULL;
	}

	bnx2i_free_mp_bdt(hba);

	if (hba->pcidev)
		pci_dev_put(hba->pcidev);
	hba->pcidev = NULL;
#ifdef __VMKLNX__
	scsi_remove_host(hba->shost);
#endif
}


/**
 * bnx2i_init_one - initialize an adapter instance and allocate necessary
 *		memory resources
 *
 * @hba: 		bnx2i adapter instance
 * @cnic: 		cnic device handle
 *
 * Global resource lock and host adapter lock is held during critical sections
 *	below. This routine is called from cnic_register_driver() context and
 *	work horse thread which does majority of device specific initialization
 **/
static int bnx2i_init_one(struct bnx2i_hba *hba, struct cnic_dev *cnic)
{
	int rc;

	mutex_lock(&bnx2i_dev_lock);
	hba->netdev = cnic->netdev;
	if (bnx2i_reg_device &&
		!test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic)) {
		rc = cnic->register_device(cnic, CNIC_ULP_ISCSI,
					   (void *) hba);
		if (!rc) {
			bnx2i_reg_device++;
			hba->age++;
			set_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
		} else if (rc == -EBUSY) 	/* duplicate registration */
			printk(KERN_ALERT "bnx2i, duplicate registration"
				  	"hba=%p, cnic=%p\n", hba, cnic);
		else if (rc == -EAGAIN)
			printk(KERN_ERR "bnx2i, driver not registered\n");
		else if (rc == -EINVAL)
			printk(KERN_ERR "bnx2i, invalid type %d\n", CNIC_ULP_ISCSI);
		else
			printk(KERN_ERR "bnx2i dev reg, unknown error, %d\n", rc);

		if (rc)
			goto ret_err;
	}

	if (cnic->netdev)
		memcpy(hba->mac_addr, cnic->netdev->dev_addr, MAX_ADDR_LEN);

	/* Allocate memory & initialize the SCSI/iSCSI host templates */
	bnx2i_register_xport(hba);

#ifndef __VMKLNX__
	/* create 'sysfs' device objects */
	rc = bnx2i_register_sysfs(hba);
	if (rc)
		goto failed_sysfs_reg;

	bnx2i_tcp_port_tbl.num_required += hba->max_active_conns;
#endif
	list_add_tail(&hba->link, &adapter_list);
	adapter_count++;
	mutex_unlock(&bnx2i_dev_lock);

	return 0;

#ifndef __VMKLNX__
failed_sysfs_reg:
	bnx2i_deregister_xport(hba);
	bnx2i_free_iscsi_scsi_template(hba);
	bnx2i_unbind_adapter_devices(hba);
#endif
	cnic->unregister_device(cnic, CNIC_ULP_ISCSI);
	bnx2i_reg_device--;
	clear_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
ret_err:
	mutex_unlock(&bnx2i_dev_lock);
	return rc;
}


/**
 * bnx2i_ulp_init - initialize an adapter instance
 *
 * @dev: 		cnic device handle
 *
 * Called from cnic_register_driver() context to initialize all enumerated
 *	cnic devices. This routine allocate adapter structure and other
 *	device specific resources.
 **/
void bnx2i_ulp_init(struct cnic_dev *dev)
{
	struct bnx2i_hba *hba;

	/* Allocate a HBA structure for this device */
	hba = bnx2i_alloc_hba(dev);
	if (!hba) {
		printk(KERN_ERR "bnx2i init: hba initialization failed\n");
		return;
	}

	/* Get PCI related information and update hba struct members */
	clear_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
	if (bnx2i_init_one(hba, dev)) {
		printk(KERN_ERR "bnx2i - hba %p init failed\n", hba);
		bnx2i_free_hba(hba);
	} else
		hba->cnic = dev;
}


/**
 * bnx2i_ulp_exit - shuts down adapter instance and frees all resources
 *
 * @dev: 		cnic device handle
 *
 **/
void bnx2i_ulp_exit(struct cnic_dev *dev)
{
	struct bnx2i_hba *hba;

	hba = bnx2i_find_hba_for_cnic(dev);
	if (!hba) {
		printk(KERN_INFO "bnx2i_ulp_exit: hba not "
				 "found, dev 0x%p\n", dev);
		return;
	}
	mutex_lock(&bnx2i_dev_lock);
	list_del_init(&hba->link);
	bnx2i_tcp_port_tbl.num_required -= hba->max_active_conns;
	adapter_count--;

	/* cleanup 'sysfs' devices and classes */
	bnx2i_unregister_sysfs(hba);

	if (test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic)) {
		hba->cnic->unregister_device(hba->cnic, CNIC_ULP_ISCSI);
		clear_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
		bnx2i_reg_device--;
#ifndef __VMKLNX__
		bnx2i_unbind_adapter_devices(hba);
#endif
	}
	mutex_unlock(&bnx2i_dev_lock);

	if (hba->pcidev)
		pci_dev_put(hba->pcidev);

	bnx2i_deregister_xport(hba);
	bnx2i_free_iscsi_scsi_template(hba);
	bnx2i_free_hba(hba);
}


/**
 * bnx2i_mod_init - module init entry point
 *
 * initialize any driver wide global data structures such as endpoint pool,
 *	tcp port manager/queue, sysfs. finally driver will register itself
 *	with the cnic module
 **/
static int __init bnx2i_mod_init(void)
{
	int rc;

	printk(KERN_INFO "%s", version);

	INIT_LIST_HEAD(&adapter_list);
	adapter_count = 0;

	mutex_init(&bnx2i_dev_lock);
	rc = bnx2i_alloc_ep_pool();
	if (rc) {
		printk(KERN_ERR "bnx2i: unable to alloc ep pool\n");
		goto out;
	}

#ifndef __VMKLNX__
	rc = bnx2i_init_tcp_port_mngr();
	if (rc) {
		printk(KERN_ERR "bnx2i: unable init TCP port manager\n");
		goto tcp_mgr_err;
	}

	/* create 'sysfs' class object */
	rc = bnx2i_sysfs_setup();
	if (rc)
		goto sysfs_err;
#endif

	rc = cnic_register_driver(CNIC_ULP_ISCSI, &bnx2i_cnic_cb);
	if (rc) {
		printk(KERN_ERR "bnx2i: unable to register driver with cnic module\n");
		goto drv_reg_failed;
	}

#ifdef __VMKLNX__
	bnx2i_lookup_iscsi_hba_vm();
#else
	bnx2i_unreg_unsupported_devices();
	bnx2i_ioctl_init();
#endif
	return 0;

drv_reg_failed:
#ifndef __VMKLNX__
	bnx2i_sysfs_cleanup();
sysfs_err:
	bnx2i_cleanup_tcp_port_mngr();
tcp_mgr_err:
#endif
	bnx2i_release_ep_pool();
out:
	return rc;
}


/**
 * bnx2i_mod_exit - module cleanup/exit entry point
 *
 * Global resource lock and host adapter lock is held during critical sections
 *	in this function. Driver will browse through the adapter list, cleans-up
 *	each instance, unregisters iscsi transport name and finally driver will
 *	unregister itself with the cnic module
 **/
static void __exit bnx2i_mod_exit(void)
{
	struct bnx2i_hba *hba;

	mutex_lock(&bnx2i_dev_lock);
	while (!list_empty(&adapter_list)) {
		hba = list_entry(adapter_list.next, struct bnx2i_hba, link);
		list_del(&hba->link);
		adapter_count--;

#ifndef __VMKLNX__
		/* cleanup 'sysfs' devices and classes */
		bnx2i_unregister_sysfs(hba);
#endif

		if (test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic)) {
			hba->cnic->unregister_device(hba->cnic, CNIC_ULP_ISCSI);
			clear_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic);
			bnx2i_reg_device--;
#ifndef __VMKLNX__
			bnx2i_unbind_adapter_devices(hba);
#endif
		}

		if (hba->pcidev)
			pci_dev_put(hba->pcidev);
		bnx2i_deregister_xport(hba);
		bnx2i_free_iscsi_scsi_template(hba);
		bnx2i_free_hba(hba);
	}
	mutex_unlock(&bnx2i_dev_lock);

#ifndef __VMKLNX__
	bnx2i_ioctl_cleanup();
#endif
	cnic_unregister_driver(CNIC_ULP_ISCSI);

#ifndef __VMKLNX__
	bnx2i_sysfs_cleanup();
	bnx2i_cleanup_tcp_port_mngr();
#endif
	bnx2i_release_ep_pool();
}

module_init(bnx2i_mod_init);
module_exit(bnx2i_mod_exit);
