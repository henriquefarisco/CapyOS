/* NoirOS USB XHCI Controller Driver
 * Implements eXtensible Host Controller Interface for USB 3.0/2.0/1.1
 */
#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>

/* PCI class/subclass for USB XHCI controller */
#define PCI_CLASS_SERIAL_BUS 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_XHCI 0x30

/* XHCI Capability Registers (offset from MMIO base) */
#define XHCI_CAPLENGTH 0x00  /* 1 byte */
#define XHCI_HCIVERSION 0x02 /* 2 bytes */
#define XHCI_HCSPARAMS1 0x04 /* 4 bytes - Structural Params 1 */
#define XHCI_HCSPARAMS2 0x08 /* 4 bytes - Structural Params 2 */
#define XHCI_HCSPARAMS3 0x0C /* 4 bytes - Structural Params 3 */
#define XHCI_HCCPARAMS1 0x10 /* 4 bytes - Capability Params 1 */
#define XHCI_DBOFF 0x14      /* 4 bytes - Doorbell Offset */
#define XHCI_RTSOFF 0x18     /* 4 bytes - Runtime Regs Offset */
#define XHCI_HCCPARAMS2 0x1C /* 4 bytes - Capability Params 2 */

/* XHCI Operational Registers (offset from MMIO base + CAPLENGTH) */
#define XHCI_USBCMD 0x00   /* USB Command */
#define XHCI_USBSTS 0x04   /* USB Status */
#define XHCI_PAGESIZE 0x08 /* Page Size */
#define XHCI_DNCTRL 0x14   /* Device Notification Control */
#define XHCI_CRCR 0x18     /* Command Ring Control */
#define XHCI_DCBAAP 0x30   /* Device Context Base Addr Array Ptr */
#define XHCI_CONFIG 0x38   /* Configure */

/* USBCMD bits */
#define XHCI_CMD_RS (1 << 0)    /* Run/Stop */
#define XHCI_CMD_HCRST (1 << 1) /* Host Controller Reset */
#define XHCI_CMD_INTE (1 << 2)  /* Interrupter Enable */
#define XHCI_CMD_HSEE (1 << 3)  /* Host System Error Enable */

/* USBSTS bits */
#define XHCI_STS_HCH (1 << 0)  /* HC Halted */
#define XHCI_STS_HSE (1 << 2)  /* Host System Error */
#define XHCI_STS_EINT (1 << 3) /* Event Interrupt */
#define XHCI_STS_PCD (1 << 4)  /* Port Change Detect */
#define XHCI_STS_CNR (1 << 11) /* Controller Not Ready */

/* Port Status and Control Register bits */
#define XHCI_PORTSC_CCS (1 << 0)        /* Current Connect Status */
#define XHCI_PORTSC_PED (1 << 1)        /* Port Enabled/Disabled */
#define XHCI_PORTSC_PR (1 << 4)         /* Port Reset */
#define XHCI_PORTSC_PLS_MASK (0xF << 5) /* Port Link State */
#define XHCI_PORTSC_PP (1 << 9)         /* Port Power */
#define XHCI_PORTSC_CSC (1 << 17)       /* Connect Status Change */
#define XHCI_PORTSC_PRC (1 << 21)       /* Port Reset Change */

/* TRB Types */
#define TRB_TYPE_NORMAL 1
#define TRB_TYPE_SETUP 2
#define TRB_TYPE_DATA 3
#define TRB_TYPE_STATUS 4
#define TRB_TYPE_LINK 6
#define TRB_TYPE_NOOP 8
#define TRB_TYPE_ENABLE_SLOT 9
#define TRB_TYPE_DISABLE_SLOT 10
#define TRB_TYPE_ADDRESS_DEV 11
#define TRB_TYPE_CONFIG_EP 12
#define TRB_TYPE_TRANSFER 32
#define TRB_TYPE_CMD_COMPLETE 33
#define TRB_TYPE_PORT_STATUS 34

/* Transfer Request Block (TRB) - 16 bytes */
struct xhci_trb {
  uint64_t param;
  uint32_t status;
  uint32_t control;
} __attribute__((packed));

/* XHCI Controller State */
struct xhci_controller {
  /* PCI location */
  uint8_t bus;
  uint8_t dev;
  uint8_t func;

  /* MMIO base addresses */
  volatile uint8_t *mmio_base;
  volatile uint8_t *op_base; /* Operational registers */
  volatile uint8_t *rt_base; /* Runtime registers */
  volatile uint8_t *db_base; /* Doorbell registers */

  /* Controller parameters */
  uint8_t max_slots;  /* Max device slots */
  uint8_t max_ports;  /* Max root hub ports */
  uint16_t max_intrs; /* Max interrupters */
  uint32_t page_size; /* Controller page size */

  /* Memory structures */
  uint64_t *dcbaa;           /* Device Context Base Addr Array */
  struct xhci_trb *cmd_ring; /* Command Ring */
  struct xhci_trb *evt_ring; /* Event Ring */
  uint32_t cmd_ring_idx;     /* Current command ring index */
  uint32_t evt_ring_idx;     /* Current event ring index */
  int cmd_ring_cycle;        /* Command ring cycle bit */

  /* State */
  int initialized;
  int running;
};

/* USB Device Slot */
struct usb_device {
  uint8_t slot_id;
  uint8_t port;
  uint8_t address;
  uint16_t vendor_id;
  uint16_t product_id;
  uint8_t class_code;
  uint8_t subclass;
  uint8_t protocol;
  int is_keyboard;
};

/* Function prototypes */
int xhci_find(struct xhci_controller *xhci);
int xhci_init(struct xhci_controller *xhci);
int xhci_reset(struct xhci_controller *xhci);
int xhci_start(struct xhci_controller *xhci);
int xhci_stop(struct xhci_controller *xhci);

/* Port operations */
int xhci_port_reset(struct xhci_controller *xhci, int port);
int xhci_port_get_status(struct xhci_controller *xhci, int port);

/* Device operations */
int xhci_enable_slot(struct xhci_controller *xhci, uint8_t *slot_id);
int xhci_address_device(struct xhci_controller *xhci, uint8_t slot_id,
                        int port);

/* Keyboard interface */
int xhci_find_keyboard(struct xhci_controller *xhci, struct usb_device *kbd);
int xhci_keyboard_poll(struct xhci_controller *xhci, struct usb_device *kbd,
                       uint8_t *key);

#endif /* XHCI_H */
