/*
 * The touch screen of the Huawei Matebook E Go 2023 (gaokun3): a Himax HX83121A capacitive
 * controller on the SPI bus of the SC8280XP's QUPv3 serial engine 6, as a source of touches for
 * /dev/hakios-touch (sys/hakios.input.h). Only with boot-arg touchscreen=1; every address below
 * belongs to this machine (its device tree: gk3.dts on top of sc8280xp.dtsi).
 *
 * There is no clock, pin, regulator or SPI framework in this kernel, so the driver does all of
 * it itself, in the order the vendor's Linux driver does:
 *   power   the block's rail is the CX power domain: asked of RPMh through the command database
 *           the boot loader left, the way hakios.mpss.c asks for the modem's rails (the device
 *           tree lists no supply for the chip itself: "always on rencently")
 *   clocks  serial engine 6 of the first QUP wrapper, by vote in the global clock controller;
 *           the rate source parented to the 19.2 MHz crystal like this selector's own frequency
 *           table's TCXO entry
 *   pins    four GPIOs to the serial engine (the device tree's "qup6" group), interrupt 175
 *           (low = a report waits) and reset 99
 *   SPI     the serial engine ("GENI") in FIFO mode, polled, at the device tree's 12 MHz ceiling
 *           rounded down through the engine's divider; the device tree asks the Linux driver for
 *           GSI DMA, we stay polled the way the phone's driver does -- the engine can do either
 *   chip    the vendor's "AHB over SPI": every access is a type 2 (write: function 0xf2, two-
 *           byte header) or type 3 (read: function 0xf3, three-byte header ending in a dummy
 *           byte) command whose command byte names one of the chip's AHB registers: 0x00 sets
 *           the address, 0x0c switches the direction to read, 0x08 pulls the data out, 0x13
 *           makes the IC take data continuously, 0x0d auto-increments the address by four per
 *           beat. (There is a type 4, function 0xf4, which the vendor file marks as Huawei's
 *           unknown: see the TODO at the frame read.) Reset is: drive the active-low pin low
 *           5 ms, release it high, wait 60 ms while the chip re-reads its firmware out of its
 *           own flash. The report is a burst read of command 0x30, the "event stack": a 40 x 60
 *           mutual-capacitance frame of 16-bit samples around 0x8000 (idle), which the vendor
 *           turns into fingers by peak finding plus a weighted centroid; we do the same, five
 *           at most. The frame's peaks carry no finger numbers, so a finger keeps its number by
 *           sitting near where that number was last frame.
 * No interrupt is used: a thread looks at the interrupt line 125 times a second (boot-arg
 * touch_poll_us=N: every N microseconds instead of 8000), like the phone's driver.
 *
 * The bring-up runs in stages, each with a line before it and an answer after for the operator
 * to watch, and boot-arg himax_stop=N stops after stage N: 1 the CX domain, 2 the clocks and
 * the engine, 3 the pins, 4 reset and chip identification, 5 the reports (the default, a full
 * run). himax_stop=0 only reads the boot arguments.
 *
 * Coordinates: the device tree says touchscreen-inverted-x and touchscreen-swapped-x-y and notes
 * that the inversion comes first, because the two do not commute. See himax_report below; the
 * panel is 1600 x 2560 (gk3.dts's own framebuffer0).
 *
 * Register layouts: Linux drivers/spi/{spi-geni-qcom.c,himax-spi.c}, drivers/soc/qcom/{qcom-
 * geni-se,rpmh-rsc,cmd-db}.c, drivers/clk/qcom/gcc-sc8280xp.c, drivers/pinctrl/qcom/pinctrl-
 * sc8280xp.c, and this machine's own device tree.
 */
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern vm_offset_t ml_io_map_wcomb(vm_offset_t phys_addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t thread);

/* RPMh's sequencer (TCS 0 of the RSC): the audio DSP's and the modem's drivers write the same slots, so every request goes through this lock */
extern lck_mtx_t hakios_rsc_lock;

static vm_offset_t tlmm, gcc, se, rsc, cmd_db;

static inline uint32_t
rd(vm_offset_t address)
{
	return *(volatile uint32_t *)address;
}

static inline void
wr(vm_offset_t address, uint32_t value)
{
	*(volatile uint32_t *)address = value;
}

static void
sleep_us(uint32_t us)
{
	delay_for_interval(us, NSEC_PER_USEC);
}

static void
sleep_ms(uint32_t ms)
{
	delay_for_interval(ms, NSEC_PER_MSEC);
}

static uint16_t
le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

/* ---- pins (TLMM) ------------------------------------------------------------------------------- */

#define TLMM_PHYS               0x0f100000UL
#define TLMM_SIZE               0x100000UL
#define GPIO_CFG(n)             (tlmm + 0x1000UL * (n))         /* [1:0] pull, [5:2] function, [8:6] drive, [9] output */
#define GPIO_IO(n)              (tlmm + 0x1000UL * (n) + 4)     /* bit 0 in, bit 1 out */
#define PULL_NONE               0
#define PULL_DOWN               2
#define PULL_UP                 3

#define PIN_MISO                154
#define PIN_MOSI                155
#define PIN_CLK                 156
#define PIN_CS                  157
#define PIN_INT                 175     /* low = the chip has a report */
#define PIN_RESET               99      /* the device tree says active low: low = in reset, high = running */
#define PIN_FUNCTION_QUP        1       /* "qup6" as a number: the phone's driver writes the same 1 for its "qup15" group */

static void
gpio_configure(unsigned pin, unsigned function, unsigned pull, unsigned ma, bool output)
{
	wr(GPIO_CFG(pin), pull | function << 2 | (ma / 2 - 1) << 6 | (output ? 1u << 9 : 0));
}

static void
gpio_set(unsigned pin, bool high)
{
	wr(GPIO_IO(pin), high ? 2 : 0);
}

static bool
gpio_get(unsigned pin)
{
	return rd(GPIO_IO(pin)) & 1;
}

static void
gpio_output(unsigned pin, bool high, unsigned pull, unsigned ma)
{
	gpio_set(pin, high);            /* the level first, so that the pin never drives the wrong one */
	gpio_configure(pin, 0, pull, ma, true);
}

/* ---- the CX domain (RPMh) ------------------------------------------------------------------------ */

#define CMD_DB_PHYS             0x80860000UL
#define CMD_DB_SIZE             0x20000UL
#define RSC_PHYS                0x18220000UL    /* the application processor's "DRV 2" */
#define RSC_SIZE                0x10000UL
#define TCS(reg)                (rsc + 0xd00 + (reg))           /* command slot set 0, an "active" one */
#define TCS_IRQ_ENABLE          0x00
#define TCS_IRQ_STATUS          0x04
#define TCS_IRQ_CLEAR           0x08
#define TCS_WAIT_FOR_COMPLETION 0x10
#define TCS_CONTROL             0x14
#define TCS_CONTROL_ENABLE      (1u << 16)
#define TCS_CONTROL_TRIGGER     (1u << 24)
#define TCS_STATUS              0x18            /* 0 while busy */
#define TCS_CMD_ENABLE          0x1c
#define TCS_CMD_MSGID(n)        (0x30 + 20 * (n))
#define TCS_CMD_ADDR(n)         (0x34 + 20 * (n))
#define TCS_CMD_DATA(n)         (0x38 + 20 * (n))
#define MSGID_WRITE_4_BYTES     0x10008
#define MSGID_RESPONSE          0x100

/* What we ask the CX domain for: 384, the level the modem and Wi-Fi drivers ask too. A vote is a
   floor RPMh takes the maximum of, so asking this low cannot switch anything already up off. */
#define CX_LEVEL                384

struct cmd_db_slave {
	uint16_t id, header_offset, data_offset, count, version, reserved[3];
};
struct cmd_db_header {
	uint32_t version;
	uint8_t magic[4];
	struct cmd_db_slave slave[8];
	uint32_t checksum, reserved;
	uint8_t data[];
};
struct cmd_db_entry {
	char id[8];
	uint32_t priority[2];
	uint32_t address;
	uint16_t length, offset;
};

/*
 * A ".lvl" resource in the boot loader's list (cx.lvl, mx.lvl, ...): its address, and where in
 * the entry's own table of levels the first one at least `target` sits; -1 for nothing high
 * enough, -2 for a list that is not there. The way to ask RPMh for such a rail is the
 * position in that table, which is how hakios.mpss.c asks for the modem's cx.lvl.
 */
static int
rpmh_level_position(const char *name, uint32_t target, uint32_t *address)
{
	const struct cmd_db_header *db = (const struct cmd_db_header *)cmd_db;
	char id[8] = { 0 };

	if (db->magic[0] != 0xdb || db->magic[1] != 0x30 || db->magic[2] != 0x03 || db->magic[3] != 0x0c) {
		printf("himax: no command database (magic %02x %02x %02x %02x)\n", db->magic[0], db->magic[1], db->magic[2],
		    db->magic[3]);
		return -2;
	}
	strlcpy(id, name, sizeof(id));
	for (unsigned s = 0; s < 8; s++) {
		const struct cmd_db_slave *slave = &db->slave[s];
		const struct cmd_db_entry *entry = (const struct cmd_db_entry *)(db->data + slave->header_offset);

		if (slave->id == 0 || slave->header_offset >= CMD_DB_SIZE - 0x1000 || slave->count > 512) {
			continue;
		}
		for (unsigned i = 0; i < slave->count; i++) {
			if (memcmp(entry[i].id, id, sizeof(id)) != 0) {
				continue;
			}
			const uint8_t *levels = db->data + slave->data_offset + entry[i].offset;

			if ((unsigned)slave->data_offset + entry[i].offset + entry[i].length > CMD_DB_SIZE - 0x1000) {
				printf("himax: the levels of \"%s\" run past the end of the list\n", name);
				return -1;
			}
			*address = entry[i].address;
			printf("himax: \"%s\" is at %#x, its levels:%s", name, entry[i].address,
			    entry[i].length / 2u ? "" : " none listed");
			for (unsigned l = 0; l < entry[i].length / 2u && l < 16; l++) {
				printf(" %u", le16(levels + 2 * l));
			}
			printf("\n");
			for (unsigned l = 0; l < entry[i].length / 2u && l < 16; l++) {
				if (le16(levels + 2 * l) >= target) {
					return (int)l;
				}
			}
			return -1;
		}
	}
	printf("himax: RPMh has no \"%s\"\n", name);
	return -1;
}

static bool
rpmh_wait(uint32_t reg, uint32_t mask, uint32_t value, const char *what)
{
	for (int i = 0; i < 2000; i++) {
		if ((rd(TCS(reg)) & mask) == value) {
			return true;
		}
		sleep_us(100);
	}
	printf("himax: RPMh: timeout waiting for %s (register %#x = %#x)\n", what, reg, rd(TCS(reg)));
	return false;
}

/* Send up to three register writes to RPMh and wait until they are done. */
static bool
rpmh_write(const uint32_t *address, const uint32_t *data, unsigned count)
{
	uint32_t irq_enable;
	bool done;

	/* the other drivers that write this slot set take the same lock, so that two requests never mix their address and data words */
	lck_mtx_lock(&hakios_rsc_lock);
	irq_enable = rd(TCS(TCS_IRQ_ENABLE));
	/* the slot set must be idle, and switched off the way the hardware wants it: trigger, then enable */
	for (int i = 0; rd(TCS(TCS_STATUS)) == 0; i++) {
		if (i > 2000) {
			printf("himax: RPMh: the command slots stay busy\n");
			lck_mtx_unlock(&hakios_rsc_lock);
			return false;
		}
		sleep_us(100);
	}
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_TRIGGER);
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_ENABLE);
	wr(TCS(TCS_IRQ_CLEAR), 1);
	for (unsigned i = 0; i < count; i++) {
		wr(TCS(TCS_CMD_MSGID(i)), MSGID_WRITE_4_BYTES | MSGID_RESPONSE);
		wr(TCS(TCS_CMD_ADDR(i)), address[i]);
		wr(TCS(TCS_CMD_DATA(i)), data[i]);
	}
	wr(TCS(TCS_WAIT_FOR_COMPLETION), (1u << count) - 1);
	wr(TCS(TCS_CMD_ENABLE), (1u << count) - 1);
	wr(TCS(TCS_IRQ_ENABLE), irq_enable | 1);        /* the status bit follows the enable; the interrupt itself stays masked */
	wr(TCS(TCS_CONTROL), TCS_CONTROL_ENABLE);
	wr(TCS(TCS_CONTROL), TCS_CONTROL_ENABLE | TCS_CONTROL_TRIGGER);

	done = rpmh_wait(TCS_IRQ_STATUS, 1, 1, "the request");
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_TRIGGER);
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_ENABLE);
	wr(TCS(TCS_CMD_ENABLE), 0);
	wr(TCS(TCS_WAIT_FOR_COMPLETION), 0);
	wr(TCS(TCS_IRQ_CLEAR), 1);
	wr(TCS(TCS_IRQ_ENABLE), irq_enable);
	lck_mtx_unlock(&hakios_rsc_lock);
	return done;
}

/*
 * The device tree hangs both the serial engine and the clock controller off the CX power
 * domain (power-domains = <&rpmhpd SC8280XP_CX>), whose RPMh resource is "cx.lvl". Asking for
 * it the way the modem's driver does is this kernel's "switch the domain on": on other machines
 * it is what the regulators behind it prove working (the phone's driver switches its I/O LDO
 * through these same helpers).
 */
static bool
cx_domain_on(void)
{
	uint32_t address = 0, value;
	int position = rpmh_level_position("cx.lvl", CX_LEVEL, &address);

	if (position < 0) {
		printf("himax: no level %u or above for the CX domain\n", CX_LEVEL);
		return false;
	}
	value = (uint32_t)position;
	printf("himax: asking RPMh, through its own command slots, for cx.lvl (%#x) = position %d, level %u or above\n",
	    address, position, CX_LEVEL);
	if (!rpmh_write(&address, &value, 1)) {
		return false;
	}
	printf("himax: CX domain asked for; RPMh has acknowledged it\n");
	return true;
}

/* ---- clocks (GCC) ------------------------------------------------------------------------------- */

#define GCC_PHYS                0x00100000UL
#define GCC_SIZE                0x60000UL
#define GCC_VOTE                (gcc + 0x52008)         /* the application processor's votes: every wrap-0 branch's enable register in gcc-sc8280xp.c */
#define VOTE_QUP0_M_AHB         (1u << 6)
#define VOTE_QUP0_S_AHB         (1u << 7)
#define VOTE_QUP0_CORE          (1u << 8)
#define VOTE_QUP0_CORE_2X       (1u << 9)
#define VOTE_QUP0_SE6           (1u << 16)              /* serial engine 6 of the first wrapper */
#define GCC_QUP0_M_AHB_CBCR     (gcc + 0x17004)         /* bit 31: the clock is off */
#define GCC_QUP0_S_AHB_CBCR     (gcc + 0x17008)
#define GCC_QUP0_SE6_CBCR       (gcc + 0x17864)
#define GCC_QUP0_SE6_RCTR       (gcc + 0x17868)         /* the rate selector (cmd_rcgr): what it reads now */
#define GCC_QUP0_SE6_CMD        (gcc + 0x1786c)         /* bit 0: take the new configuration */
#define GCC_QUP0_SE6_CFG        (gcc + 0x17870)         /* [4:0] 2*divider-1, [10:8] source, [13:12] mode */
#define GCC_QUP0_SE6_DFSR       (gcc + 0x17880)         /* bit 0: the selector chooses among preset rates itself */
#define CLOCK_OFF               (1u << 31)
#define TCXO_HZ                 19200000

static uint32_t se_source_hz;

/*
 * Parent the engine's clock to the crystal, undivided: this very selector's frequency table has
 * F(19200000, P_BI_TCXO, 1, 0, 0), and the parent map numbers that source 0, so the new
 * configuration is the old one with the source field cleared to 0 and the divider field set to
 * the divide-by-one value -- the field layout proven on the equivalent selector on the phone.
 * The preset-rates bit is printed but not honoured as a second path: SPI6 is "disabled" in the
 * device tree, so nothing ran through this engine before us and its presets are not in use.
 */
static bool
clocks_on(void)
{
	printf("himax: the votes read %#x, the branch states %#x %#x %#x, and the rate selector %#x %#x %#x %#x\n",
	    rd(GCC_VOTE), rd(GCC_QUP0_M_AHB_CBCR), rd(GCC_QUP0_S_AHB_CBCR), rd(GCC_QUP0_SE6_CBCR), rd(GCC_QUP0_SE6_RCTR),
	    rd(GCC_QUP0_SE6_CMD), rd(GCC_QUP0_SE6_CFG), rd(GCC_QUP0_SE6_DFSR));
	if (rd(GCC_QUP0_SE6_DFSR) & 1) {
		printf("himax: the selector has its preset rates enabled (%#x): writing a rate anyway and watching\n",
		    rd(GCC_QUP0_SE6_DFSR));
	}
	wr(GCC_VOTE, rd(GCC_VOTE) | VOTE_QUP0_M_AHB | VOTE_QUP0_S_AHB | VOTE_QUP0_CORE | VOTE_QUP0_CORE_2X);
	wr(GCC_QUP0_SE6_CFG, (rd(GCC_QUP0_SE6_CFG) & ~0x371fu) | 1);
	wr(GCC_QUP0_SE6_CMD, rd(GCC_QUP0_SE6_CMD) | 1);
	for (int i = 0; i < 500 && (rd(GCC_QUP0_SE6_CMD) & 1); i++) {
		sleep_us(10);
	}
	if (rd(GCC_QUP0_SE6_CMD) & 1) {
		printf("himax: the serial engine's clock does not take its configuration (%#x)\n", rd(GCC_QUP0_SE6_CMD));
		return false;
	}
	printf("himax: the rate selector now reads %#x %#x %#x: the engine runs off the crystal at %u Hz\n",
	    rd(GCC_QUP0_SE6_RCTR), rd(GCC_QUP0_SE6_CMD), rd(GCC_QUP0_SE6_CFG), TCXO_HZ);
	se_source_hz = TCXO_HZ;
	wr(GCC_VOTE, rd(GCC_VOTE) | VOTE_QUP0_SE6);
	for (int i = 0; i < 1000; i++) {
		if (!((rd(GCC_QUP0_M_AHB_CBCR) | rd(GCC_QUP0_S_AHB_CBCR) | rd(GCC_QUP0_SE6_CBCR)) & CLOCK_OFF)) {
			return true;
		}
		sleep_us(10);
	}
	printf("himax: clocks stay off: %#x %#x %#x\n", rd(GCC_QUP0_M_AHB_CBCR), rd(GCC_QUP0_S_AHB_CBCR),
	    rd(GCC_QUP0_SE6_CBCR));
	return false;
}

/* ---- SPI through the serial engine (GENI) ---------------------------------------------------------- */

#define SE_PHYS                 0x00998000UL
#define SE_SIZE                 0x4000UL
#define GENI_FORCE_DEFAULT      (se + 0x020)
#define GENI_OUTPUT_CTRL        (se + 0x024)
#define GENI_CGC_CTRL           (se + 0x028)
#define GENI_STATUS             (se + 0x040)    /* bit 0: a command is running */
#define GENI_SER_M_CLK_CFG      (se + 0x048)    /* bit 0 enable, [15:4] divider */
#define GENI_IF_DISABLE_RO      (se + 0x064)    /* bit 0: no FIFO interface */
#define GENI_FW_REVISION_RO     (se + 0x068)    /* [15:8] protocol: 1 = SPI */
#define GENI_CLK_SEL            (se + 0x07c)
#define SPI_CPHA                (se + 0x224)
#define SPI_LOOPBACK            (se + 0x22c)
#define SPI_CPOL                (se + 0x230)
#define SPI_DEMUX_OUTPUT_INV    (se + 0x24c)
#define SPI_DEMUX_SEL           (se + 0x250)
#define GENI_BYTE_GRAN          (se + 0x254)
#define GENI_DMA_MODE_EN        (se + 0x258)
#define SPI_TRANS_CFG           (se + 0x25c)
#define GENI_TX_PACKING_CFG0    (se + 0x260)
#define GENI_TX_PACKING_CFG1    (se + 0x264)
#define SPI_WORD_LEN            (se + 0x268)
#define SPI_TX_TRANS_LEN        (se + 0x26c)
#define SPI_RX_TRANS_LEN        (se + 0x270)
#define SPI_DELAY_COUNTERS      (se + 0x278)    /* [7:0] CS asserted before the first clock, in clock cycles */
#define GENI_RX_PACKING_CFG0    (se + 0x284)
#define GENI_RX_PACKING_CFG1    (se + 0x288)
#define GENI_M_CMD0             (se + 0x600)    /* [31:27] operation */
#define GENI_M_CMD_CTRL         (se + 0x604)    /* bit 2 cancel, bit 1 abort */
#define GENI_M_IRQ_STATUS       (se + 0x610)
#define GENI_M_IRQ_EN           (se + 0x614)
#define GENI_M_IRQ_CLEAR        (se + 0x618)
#define GENI_S_IRQ_CLEAR        (se + 0x648)
#define GENI_TX_FIFO            (se + 0x700)
#define GENI_RX_FIFO            (se + 0x780)
#define GENI_TX_FIFO_STATUS     (se + 0x800)    /* [27:0] words */
#define GENI_RX_FIFO_STATUS     (se + 0x804)    /* bit 31 last, [30:28] bytes of the last word, [24:0] words */
#define GENI_TX_WATERMARK       (se + 0x80c)
#define GENI_RX_WATERMARK       (se + 0x810)
#define GENI_RX_RFR_WATERMARK   (se + 0x814)
#define SE_DMA_TX_IRQ_CLR       (se + 0xc44)
#define SE_DMA_RX_IRQ_CLR       (se + 0xd44)
#define SE_GSI_EVENT_EN         (se + 0xe18)
#define SE_IRQ_EN               (se + 0xe1c)
#define SE_HW_PARAM_0           (se + 0xe24)    /* [21:16] FIFO depth in words */
#define SE_DMA_GENERAL_CFG      (se + 0xe30)

#define M_CMD_DONE              (1u << 0)
#define M_CMD_CANCEL            (1u << 4)
#define M_CMD_ABORT             (1u << 5)
#define M_ERRORS                ((1u << 1) | (1u << 2) | (1u << 3) | (1u << 24) | (1u << 25) | (1u << 28) | (1u << 29))
#define SPI_FULL_DUPLEX         (3u << 27)

static uint32_t fifo_depth;

/* 0 = ready; otherwise why the engine cannot be used */
static const char *
geni_spi_init(uint32_t hz)
{
	uint32_t divider, actual;

	if ((rd(GENI_FW_REVISION_RO) >> 8 & 0xff) != 1) {
		return "the serial engine has no SPI firmware";
	}
	if (rd(GENI_IF_DISABLE_RO) & 1) {
		return "the serial engine's FIFO interface is disabled";
	}
	fifo_depth = rd(SE_HW_PARAM_0) >> 16 & 0x3f;
	if (fifo_depth < 4) {
		return "the serial engine reports no FIFO";
	}
	printf("himax: the engine reads: firmware %#x, interface %#x, parameters %#x\n", rd(GENI_FW_REVISION_RO),
	    rd(GENI_IF_DISABLE_RO), rd(SE_HW_PARAM_0));
	wr(SE_GSI_EVENT_EN, 0);
	wr(GENI_M_IRQ_CLEAR, 0xffffffff);
	wr(GENI_S_IRQ_CLEAR, 0xffffffff);
	wr(SE_DMA_TX_IRQ_CLR, 0xffffffff);
	wr(SE_DMA_RX_IRQ_CLR, 0xffffffff);
	wr(GENI_CGC_CTRL, rd(GENI_CGC_CTRL) | 0x7f);
	wr(SE_DMA_GENERAL_CFG, rd(SE_DMA_GENERAL_CFG) | 0xf);
	wr(GENI_OUTPUT_CTRL, 0x7f);
	wr(GENI_FORCE_DEFAULT, 1);
	wr(GENI_DMA_MODE_EN, rd(GENI_DMA_MODE_EN) & ~1u);
	wr(GENI_RX_WATERMARK, fifo_depth - 3);
	wr(GENI_RX_RFR_WATERMARK, fifo_depth - 2);
	wr(GENI_TX_WATERMARK, 0);
	/* the status bits we poll follow these enables; the interrupt itself stays masked at the GIC */
	wr(SE_IRQ_EN, rd(SE_IRQ_EN) | 0xf);
	wr(GENI_M_IRQ_EN, rd(GENI_M_IRQ_EN) | 0x7fc0007f);

	/*
	 * mode 3 (clock idle high, data out on the falling edge): the vendor's driver sets its
	 * SPI device to SPI_MODE_3. Chip select 0, 8 bits, most significant first, four bytes in
	 * a FIFO word.
	 */
	wr(SPI_LOOPBACK, 0);
	wr(SPI_DEMUX_SEL, 0);
	wr(SPI_CPHA, 1);
	wr(SPI_CPOL, 1);
	wr(SPI_DEMUX_OUTPUT_INV, 0);
	wr(SPI_TRANS_CFG, rd(SPI_TRANS_CFG) & ~2u);
	wr(GENI_TX_PACKING_CFG0, 0x0007f8fe);
	wr(GENI_TX_PACKING_CFG1, 0x000ffefe);
	wr(GENI_RX_PACKING_CFG0, 0x0007f8fe);
	wr(GENI_RX_PACKING_CFG1, 0x000ffefe);
	wr(GENI_BYTE_GRAN, 0);
	wr(SPI_WORD_LEN, 8 - 4);

	/*
	 * 12 MHz is not an exact divisor of the crystal, so the bus runs just under it (19.2 / 2
	 * = 9.6), as fast as the device tree allows without exceeding it. The vendor's driver
	 * also asks the SPI core for 300 ns between asserting CS and the first clock
	 * (HIMAX_SPI_CS_SETUP_TIME): that is what the delay counters' low byte is for, so ask
	 * for just enough whole cycles to cover it.
	 */
	divider = (se_source_hz + hz - 1) / hz;
	actual = se_source_hz / divider;
	wr(SPI_DELAY_COUNTERS, (unsigned)((300ull * actual + 999999999ull) / 1000000000ull));
	printf("himax: divider %u: %u Hz on the wire, CS set up over %u clock cycles (%#x)\n", divider, actual,
	    rd(SPI_DELAY_COUNTERS) & 0xff, rd(SPI_DELAY_COUNTERS));
	wr(GENI_CLK_SEL, 0);
	wr(GENI_SER_M_CLK_CFG, (divider ? divider : 1) << 4 | 1);
	return NULL;
}

static void
geni_drain(uint8_t *rx, uint32_t length, uint32_t *received)
{
	uint32_t status = rd(GENI_RX_FIFO_STATUS), words = status & 0x01ffffff, bytes = words * 4;

	if (status & 0x80000000u) {
		uint32_t valid = status >> 28 & 7;

		if (valid && valid < 4) {
			bytes -= 4 - valid;
		}
	}
	for (uint32_t i = 0; i < words; i++) {
		uint32_t word = rd(GENI_RX_FIFO);

		for (int j = 0; j < 4 && bytes; j++, bytes--) {
			if (*received < length) {
				if (rx) {
					rx[*received] = (uint8_t)(word >> (8 * j));
				}
				(*received)++;
			}
		}
	}
}

static void
geni_recover(void)
{
	wr(GENI_M_CMD_CTRL, 1u << 2);
	for (int i = 0; i < 2000 && !(rd(GENI_M_IRQ_STATUS) & M_CMD_CANCEL); i++) {
		sleep_us(100);
	}
	if (!(rd(GENI_M_IRQ_STATUS) & M_CMD_CANCEL)) {
		wr(GENI_M_CMD_CTRL, 1u << 1);
		for (int i = 0; i < 2000 && !(rd(GENI_M_IRQ_STATUS) & M_CMD_ABORT); i++) {
			sleep_us(100);
		}
	}
	for (int i = 0; i < 64 && (rd(GENI_RX_FIFO_STATUS) & 0x01ffffff); i++) {
		(void)rd(GENI_RX_FIFO);
	}
	wr(GENI_M_IRQ_CLEAR, 0xffffffff);
}

/* One transfer with the chip selected throughout: `length` bytes out (zeros without tx) and as many in. */
static bool
geni_transfer(const uint8_t *tx, uint8_t *rx, uint32_t length)
{
	static unsigned complaints;
	uint32_t sent = 0, received = 0, status = 0;
	uint64_t deadline;

	for (int i = 0; rd(GENI_STATUS) & 1; i++) {
		if (i > 2000) {
			geni_recover();
			break;
		}
		sleep_us(100);
	}
	wr(GENI_M_IRQ_CLEAR, 0xffffffff);
	wr(SPI_TX_TRANS_LEN, length);
	wr(SPI_RX_TRANS_LEN, length);
	wr(GENI_M_CMD0, SPI_FULL_DUPLEX);
	clock_interval_to_deadline(200, NSEC_PER_MSEC, &deadline);
	while (mach_absolute_time() < deadline) {
		while (sent < length && (rd(GENI_TX_FIFO_STATUS) & 0x0fffffff) + 1 < fifo_depth) {
			uint32_t word = 0;

			for (int j = 0; j < 4 && sent < length; j++, sent++) {
				word |= (uint32_t)(tx ? tx[sent] : 0) << (8 * j);
			}
			wr(GENI_TX_FIFO, word);
		}
		status = rd(GENI_M_IRQ_STATUS);
		geni_drain(rx, length, &received);
		if (status & M_ERRORS) {
			break;
		}
		if (status & M_CMD_DONE) {
			geni_drain(rx, length, &received);
			wr(GENI_M_IRQ_CLEAR, status);
			if (received == length) {
				return true;
			}
			break;
		}
	}
	if (complaints++ < 8) {
		printf("himax: SPI transfer of %u bytes failed: status %#x, sent %u, received %u\n", length, status, sent,
		    received);
	}
	geni_recover();
	return false;
}

static bool (*spi_transfer)(const uint8_t *tx, uint8_t *rx, uint32_t length);

/* ---- the Himax's "AHB over SPI" ------------------------------------------------------------------- */

/*
 * The first byte of every command is the SPI function: 0xf2 is type 2, write (two-byte header,
 * then what the named command wants); 0xf3 is type 3, read (three-byte header: function,
 * command, and a dummy byte while the IC thinks). The second byte names one of the IC's AHB
 * registers, which its bus bridge answers directly; through them, its hardware registers and
 * SRAM. The vendor file wonders about a type 4 (0xf4) for Huawei; everything here is what the
 * file actually does on this chip.
 */
#define HIMAX_FUNCTION_READ             0xf3
#define HIMAX_FUNCTION_WRITE            0xf2
#define HIMAX_BUS_R_HLEN                3U              /* function, command, dummy */
#define HIMAX_BUS_W_HLEN                2U              /* function, command */
#define HIMAX_REG_SZ                    4U

/* AHB registers, by their command byte */
#define HIMAX_AHB_ADDR_BYTE_0           0x00            /* the address to work on */
#define HIMAX_AHB_ADDR_RDATA_BYTE_0     0x08            /* the data of the read set up */
#define HIMAX_AHB_ADDR_ACCESS_DIRECTION 0x0c            /* 0: read */
#define HIMAX_AHB_ADDR_BURST            0x0d            /* 0x10; 0x13 to auto-add four per beat */
#define HIMAX_AHB_ADDR_CONTI            0x13            /* 0x31: take data continuously */
#define HIMAX_AHB_ADDR_EVENT_STACK      0x30            /* the touch frame, read in a burst */
#define HIMAX_AHB_CMD_ACCESS_DIRECTION_READ 0x00
#define HIMAX_AHB_CMD_CONTI             0x31
#define HIMAX_AHB_CMD_BURST             0x10
#define HIMAX_AHB_BURST_INCR4           0x03

/* hardware registers, reached through the AHB path */
#define HIMAX_REG_ADDR_FW_STATUS        0x900000a8
#define HIMAX_REG_ADDR_ICID             0x900000d0
#define HIMAX_REG_DATA_FW_STATE_RUNNING 0x05
#define HIMAX_REG_DATA_ICID             0x83121a        /* what a HX83121A says, as the vendor decodes it (>> 8) */

/* the one runtime setting the vendor insists on for this exact machine */
#define HIMAX_DSRAM_ADDR_RAW_OUT_SEL    0x100072ec
#define HIMAX_DATA_RAW_OUT_SEL          0xf6

/* the frame read (what those four are is explained at the report section below); sized so that
   every bus helper and the report share one pair of buffers big enough for the largest read */
#define HIMAX_MAX_TX                    40              /* the rows of the frame */
#define HIMAX_MAX_RX                    60              /* and its columns */
#define HIMAX_FRAME_OFF                 4               /* the vendor's OFST, from the start of the receive buffer */
#define HIMAX_FRAME_BYTES               (2 * HIMAX_MAX_TX * HIMAX_MAX_RX)
#define HIMAX_REPORT_BYTES              (HIMAX_FRAME_OFF + HIMAX_FRAME_BYTES)

#define HIMAX_MAX_READ                  (HIMAX_BUS_R_HLEN + HIMAX_REPORT_BYTES)
static uint8_t himax_tx[HIMAX_MAX_READ + 8], himax_rx[HIMAX_MAX_READ + 8];

/* the chip leaves address auto-increment on through a reset; the vendor driver's ts->incr4_en */
static bool himax_incr4 = true;

/* Type 3: header out, answer in after three bytes. */
static bool
himax_spi_read(uint8_t command, uint8_t *buf, uint32_t length)
{
	uint32_t total = HIMAX_BUS_R_HLEN + length;

	if (total > sizeof(himax_tx) || total > sizeof(himax_rx)) {
		return false;
	}
	memset(himax_tx, 0, total);
	himax_tx[0] = HIMAX_FUNCTION_READ;
	himax_tx[1] = command;
	himax_tx[2] = 0x00;                             /* the dummy byte for the IC to process the command */
	if (!spi_transfer(himax_tx, himax_rx, total)) {
		return false;
	}
	memcpy(buf, himax_rx + HIMAX_BUS_R_HLEN, length);
	return true;
}

/* Type 2: header, then the four address bytes if the command is one that takes an address, then data. */
static bool
himax_spi_write(uint8_t command, const uint8_t *address, const uint8_t *data, uint32_t length)
{
	uint32_t total = HIMAX_BUS_W_HLEN + (address ? HIMAX_REG_SZ : 0) + length;

	if (total > sizeof(himax_tx)) {
		return false;
	}
	memset(himax_tx, 0, total);
	himax_tx[0] = HIMAX_FUNCTION_WRITE;
	himax_tx[1] = command;
	if (address) {
		memcpy(himax_tx + HIMAX_BUS_W_HLEN, address, HIMAX_REG_SZ);
	}
	if (data) {
		memcpy(himax_tx + HIMAX_BUS_W_HLEN + (address ? HIMAX_REG_SZ : 0), data, length);
	}
	return spi_transfer(himax_tx, NULL, total);
}

static bool
himax_set_burst_mode(bool auto_add_4_byte)
{
	const uint8_t conti = HIMAX_AHB_CMD_CONTI;
	uint8_t burst = HIMAX_AHB_CMD_BURST;

	if (auto_add_4_byte == himax_incr4) {
		return true;
	}
	burst |= auto_add_4_byte ? HIMAX_AHB_BURST_INCR4 : 0;
	if (!himax_spi_write(HIMAX_AHB_ADDR_CONTI, NULL, &conti, 1) ||
	    !himax_spi_write(HIMAX_AHB_ADDR_BURST, NULL, &burst, 1)) {
		return false;
	}
	himax_incr4 = auto_add_4_byte;
	return true;
}

/*
 * Read a chip register or SRAM word: write the address to AHB register 0x00, switch the
 * direction to read, pull the answer out of AHB register 0x08 -- verbatim the shape of the
 * vendor's himax_mcu_register_read(), the framing this file is sure of.
 */
static bool
himax_register_read(uint32_t address, uint8_t *buf, uint32_t length)
{
	uint8_t target[4] = { (uint8_t)address, (uint8_t)(address >> 8), (uint8_t)(address >> 16), (uint8_t)(address >> 24) };
	const uint8_t direction = HIMAX_AHB_CMD_ACCESS_DIRECTION_READ;

	if (!himax_set_burst_mode(length > HIMAX_REG_SZ)) {
		return false;
	}
	if (!himax_spi_write(HIMAX_AHB_ADDR_BYTE_0, target, NULL, 0) ||
	    !himax_spi_write(HIMAX_AHB_ADDR_ACCESS_DIRECTION, NULL, &direction, 1)) {
		return false;
	}
	return himax_spi_read(HIMAX_AHB_ADDR_RDATA_BYTE_0, buf, length);
}

static bool
himax_register_write(uint32_t address, const uint8_t *buf, uint32_t length)
{
	uint8_t target[4] = { (uint8_t)address, (uint8_t)(address >> 8), (uint8_t)(address >> 16), (uint8_t)(address >> 24) };

	if (!himax_set_burst_mode(length > HIMAX_REG_SZ)) {
		return false;
	}
	return himax_spi_write(HIMAX_AHB_ADDR_BYTE_0, target, buf, length);
}

/*
 * Wake the bus: the IC sleeps between uses, and a dummy read wakes it. Then set the no-auto-
 * increment state and confirm it by reading the AHB configuration itself back, as the vendor's
 * himax_mcu_interface_on() does.
 */
static bool
himax_interface_on(void)
{
	uint8_t byte, conti = 0, burst = 0;

	if (!himax_spi_read(HIMAX_AHB_ADDR_RDATA_BYTE_0, &byte, 1)) {
		return false;
	}
	for (int attempt = 0; attempt < 10; attempt++) {
		if (!himax_set_burst_mode(false)) {
			return false;
		}
		if (himax_spi_read(HIMAX_AHB_ADDR_CONTI, &conti, 1) && himax_spi_read(HIMAX_AHB_ADDR_BURST, &burst, 1) &&
		    conti == HIMAX_AHB_CMD_CONTI && burst == HIMAX_AHB_CMD_BURST) {
			printf("himax: the bus is awake (its settings read %#x %#x)\n", conti, burst);
			return true;
		}
		printf("himax: the bus answers with its settings %#x %#x, not 0x31 0x10 (try %d)\n", conti, burst, attempt + 1);
		sleep_ms(1);
	}
	return false;
}

/*
 * The pin reset. The device tree's reset-gpios is GPIO_ACTIVE_LOW and the vendor's gpiod
 * honours that: asserting reset drives the pin LOW, for 5 ms; releasing drives it high; and
 * the 60 ms after is the chip re-reading its own firmware out of its internal flash (~55 ms),
 * not a leisurely wait.
 */
static void
himax_pin_reset(void)
{
	gpio_set(PIN_RESET, false);
	sleep_ms(5);
	gpio_set(PIN_RESET, true);
	sleep_ms(60);
	himax_incr4 = true;                             /* what the IC comes out of reset with (the vendor's comment) */
}

/* Stage 4's answer: what the chip is, and whether it is running. */
static bool
himax_probe(void)
{
	uint8_t data[4], id;
	uint32_t icid;

	printf("himax: waking the bus and reading the chip's own registers over it\n");
	if (!himax_interface_on()) {
		printf("himax: the bus never answered (its settings are not 0x31 0x10): is the chip powered at all?\n");
		return false;
	}
	/*
	 * The vendor's chip_detect stops the firmware before reading the ID (a running firmware
	 * can hold the AHB); we read as things stand, because the point of this stage is the
	 * truth the chip reports, and the status register says right after whether firmware was
	 * why an ID would read strange.
	 */
	if (!himax_register_read(HIMAX_REG_ADDR_ICID, data, sizeof(data))) {
		printf("himax: could not read the chip ID\n");
		return false;
	}
	icid = data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
	printf("himax: IC_ID (0x900000d0) is %#010x: a \"HX%06X\"\n", icid, icid >> 8 & 0xffffff);
	if ((icid >> 8) != HIMAX_REG_DATA_ICID) {
		printf("himax: that is NOT the 83121a this machine's device tree says it has\n");
	}
	if (!himax_register_read(HIMAX_REG_ADDR_FW_STATUS, data, sizeof(data))) {
		printf("himax: could not read the firmware status\n");
		return false;
	}
	id = data[0];
	printf("himax: FW_STATUS (0x900000a8) is %#010x: state byte %#02x", data[0] | (uint32_t)data[1] << 8 |
	    (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24, id);
	if (id != HIMAX_REG_DATA_FW_STATE_RUNNING) {
		printf(": the firmware is NOT running (0x05 is running); stopping here\n");
		return false;
	}
	printf(": the firmware is running\n");
	return true;
}

/*
 * Tell the firmware to put the raw mutual-capacitance frame on the event stack. The vendor's
 * comment on this write reads "RawOut select set, must be 0xf6, fuck you huawei": with any
 * other value the stack is not the 40 x 60 matrix of samples we decode below.
 */
static bool
himax_raw_frames(void)
{
	uint8_t value[4] = { HIMAX_DATA_RAW_OUT_SEL, 0, 0, 0 }, answer[4] = { 0 };

	printf("himax: asking the firmware for raw mutual-capacitance frames on the event stack\n");
	if (!himax_register_write(HIMAX_DSRAM_ADDR_RAW_OUT_SEL, value, sizeof(value)) ||
	    !himax_register_read(HIMAX_DSRAM_ADDR_RAW_OUT_SEL, answer, sizeof(answer))) {
		printf("himax: the raw-frame select could not be set\n");
		return false;
	}
	printf("himax: raw frame select written %#x, read back %#x %#x %#x %#x%s\n", HIMAX_DATA_RAW_OUT_SEL,
	    answer[0], answer[1], answer[2], answer[3],
	    answer[0] == HIMAX_DATA_RAW_OUT_SEL ? "" : " (NOT what was asked: the frames below may not decode)");
	return true;
}

/* ---- the touch report ----------------------------------------------------------------------------- */

/*
 * The event stack (command 0x30, burst-read with auto-increment on, which the vendor relies on
 * after a reset like this). Their hx83121a_gaokun_read_event_stack() streams the whole thing
 * (~5132 bytes by their own macro, FIXME marked; that includes delta and per-row data we never
 * use) into the receive buffer, and their report thread then reads 40 rows of 60 samples of
 * 16 bits starting four bytes into that buffer -- four, which includes the three-byte header
 * their own type 3 read put there. We keep exactly that: the matrix starts at byte 4 of the
 * buffer, one byte after the header.
 * TODO: that one-byte prefix is what their code does and their file marks the stack's real
 * shape FIXME; if the samples below do not hover at 0x8000, this offset is the first thing to
 * question, and the second is the type 4 (0xf4) framing the vendor file wonders about for
 * Huawei machines.
 */
/* HIMAX_MAX_TX/RX, HIMAX_FRAME_OFF, HIMAX_FRAME_BYTES and HIMAX_REPORT_BYTES: defined above, in
   the bus section, where the shared buffers are sized from them. */

/* The vendor's thresholds: a finger lifts a sample above the equilibrium; anything else is noise. */
#define EQUILIBRIUM             0x8000
#define DOWN_THRESHOLD          0x100
#define UP_THRESHOLD            0x1000

/* The frame's extent in the sensor's own coordinates, and the vendor's scaling factors. */
#define X_TOTAL                 25600U                  /* gk3.dts's touchscreen-size-x */
#define Y_TOTAL                 16000U                  /* and its -y */
#define X_SCALE                 9U
#define Y_SCALE                 1U
#define X_TOTAL_REV             (X_SCALE * X_TOTAL * (HIMAX_MAX_RX + 1) / HIMAX_MAX_RX)
#define Y_TOTAL_REV             (Y_SCALE * Y_TOTAL * (HIMAX_MAX_TX + 1) / HIMAX_MAX_TX)
#define DX                      (X_TOTAL_REV / HIMAX_MAX_RX)
#define DY                      (Y_TOTAL_REV / HIMAX_MAX_TX)

/* The panel, from gk3.dts's own framebuffer0. */
#define PANEL_X                 1600U
#define PANEL_Y                 2560U

#define HIMAX_MAX_CONTACTS      5                       /* the task set: five fingers, no more */
#define HIMAX_MAX_PEAKS         15                      /* a few candidates per finger is enough */
#define WEAK_PEAK               800                     /* below this a peak is a fingernail: the vendor drops it */

static uint32_t
mult_frac(uint32_t value, uint32_t multiply, uint32_t divide)
{
	return (uint32_t)(((uint64_t)value * multiply) / divide);
}

static uint16_t
himax_sample(int tx, int rx)
{
	return le16(himax_rx + HIMAX_FRAME_OFF + 2u * ((uint32_t)tx * HIMAX_MAX_RX + (uint32_t)rx));
}

/* Inside the frame and a real finger: the value above equilibrium, else 0 (the vendor's simple_filter). */
static uint16_t
himax_filtered(int tx, int rx)
{
	uint16_t value;

	if (tx < 0 || tx >= HIMAX_MAX_TX || rx < 0 || rx >= HIMAX_MAX_RX) {
		return 0;
	}
	value = himax_sample(tx, rx);
	return (EQUILIBRIUM + DOWN_THRESHOLD < value) && (value < EQUILIBRIUM + UP_THRESHOLD) ? (uint16_t)(value - EQUILIBRIUM) : 0;
}

/* Is there anything at all in this frame? The vendor's fast_check, shortened. */
static bool
himax_any_touch(void)
{
	int best_tx = 0, best_rx = 0;
	uint16_t best = 0;

	for (int tx = 0; tx < HIMAX_MAX_TX; tx++) {
		for (int rx = 0; rx < HIMAX_MAX_RX; rx++) {
			uint16_t walk = himax_filtered(tx, rx);

			if (walk > best) {
				best = walk;
				best_tx = tx;
				best_rx = rx;
			}
		}
	}
	/* drop the frame if the maximum stands alone: one hot channel is a spike, not a finger */
	return best != 0 && (himax_filtered(best_tx - 1, best_rx) || himax_filtered(best_tx + 1, best_rx) ||
	    himax_filtered(best_tx, best_rx - 1) || himax_filtered(best_tx, best_rx + 1));
}

struct himax_peak {
	int x, y;
	uint16_t strength;
};

/* Local maxima, strongest first: the vendor's raw2rxtx(), with an insertion sort instead of the kernel's sort(). */
static void
himax_find_peaks(struct himax_peak *peaks, unsigned *count)
{
	*count = 0;
	for (int tx = 0; tx < HIMAX_MAX_TX; tx++) {
		for (int rx = 0; rx < HIMAX_MAX_RX && *count < HIMAX_MAX_PEAKS; rx++) {
			uint16_t vc = himax_filtered(tx, rx);

			if (!vc) {
				continue;
			}
			/*
			 * greater than all eight neighbours; the strict > against the upper right
			 * is the vendor's own and is kept, their field must have needed it
			 */
			if (vc >= himax_filtered(tx, rx - 1) && vc >= himax_filtered(tx, rx + 1) &&
			    vc >= himax_filtered(tx - 1, rx) && vc >= himax_filtered(tx + 1, rx) &&
			    vc >= himax_filtered(tx - 1, rx - 1) && vc > himax_filtered(tx - 1, rx + 1) &&
			    vc >= himax_filtered(tx + 1, rx - 1) && vc >= himax_filtered(tx + 1, rx + 1)) {
				peaks[*count].x = rx;
				peaks[*count].y = tx;
				peaks[*count].strength = vc;
				(*count)++;
				rx++;                         /* the channel right of a peak is not a peak */
			}
		}
	}
	for (unsigned i = 1; i < *count; i++) {
		struct himax_peak top = peaks[i];
		unsigned j = i;

		while (j > 0 && peaks[j - 1].strength < top.strength) {
			peaks[j] = peaks[j - 1];
			j--;
		}
		peaks[j] = top;
	}
	/* drop candidates within three channels of a stronger one, the vendor's L1-distance rule */
	for (unsigned i = 0; i < *count;) {
		unsigned j = i + 1;

		while (j < *count) {
			int dx = peaks[i].x - peaks[j].x, dy = peaks[i].y - peaks[j].y;

			if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) < 3) {
				*count -= 1;
				for (unsigned k = j; k < *count; k++) {
					peaks[k] = peaks[k + 1];
				}
			} else {
				j++;
			}
		}
		i++;
	}
}

/*
 * Where the finger sits inside its peak, the vendor's rxtx2xy(): the weighted average of the
 * nine samples around it, in the sensor's 25600 x 16000 coordinates. Their comment says it
 * plainly: without the weak-point filter, fingernails register and a drawn line jumps between
 * channels.
 */
static bool
himax_centroid(const struct himax_peak *peak, uint32_t *sx, uint32_t *sy)
{
	int tx = peak->y, rx = peak->x;
	uint32_t vc = himax_filtered(tx, rx), vl = himax_filtered(tx, rx - 1), vr = himax_filtered(tx, rx + 1),
	    vu = himax_filtered(tx - 1, rx), vd = himax_filtered(tx + 1, rx), vlu = himax_filtered(tx - 1, rx - 1),
	    vld = himax_filtered(tx + 1, rx - 1), vru = himax_filtered(tx - 1, rx + 1), vrd = himax_filtered(tx + 1, rx + 1);
	uint32_t vsum = vl + vr + vc + vu + vd + vlu + vld + vru + vrd;

	if (vc < WEAK_PEAK || vsum == 0) {
		return false;
	}
	/* the vendor's mult_frac() expanded; never negative: off-frame neighbours read zero */
	*sx = mult_frac(rx * vsum + vru + vr + vrd - vlu - vl - vld, DX, X_SCALE * vsum);
	*sy = mult_frac(tx * vsum + vld + vd + vrd - vlu - vu - vru, DY, Y_SCALE * vsum);
	return true;
}

/* What has been seen so far: said once a minute, so that the end of the log always tells. */
static struct {
	uint32_t polls, reports, touches, bad, last_x, last_y;
} seen;

/*
 * Which fingers were in the last frame, and where. The frame carries peaks, not numbers, so a
 * finger keeps its number by sitting near where that number was last frame: one channel of the
 * grid is about 427 sensor units in x and 400 in y, and a finger does not jump three channels
 * between reports 8 ms apart. The bookkeeping itself is the phone driver's.
 */
#define MATCH_DISTANCE          (3 * 427)

static struct {
	bool down, seen;
	uint32_t sx, sy, x, y;
} contact[HIMAX_MAX_CONTACTS];

static void
himax_report(unsigned count, const uint32_t *sx, const uint32_t *sy)
{
	static unsigned shown;

	for (unsigned i = 0; i < count; i++) {
		uint32_t x = sx[i], y = sy[i];
		int best = -1;
		uint32_t best_distance = 0;

		if (x > X_TOTAL) {
			x = X_TOTAL;
		}
		if (y > Y_TOTAL) {
			y = Y_TOTAL;
		}
		for (unsigned id = 0; id < HIMAX_MAX_CONTACTS; id++) {
			uint32_t dx = contact[id].sx > x ? contact[id].sx - x : x - contact[id].sx;
			uint32_t dy = contact[id].sy > y ? contact[id].sy - y : y - contact[id].sy;

			if (!contact[id].down || contact[id].seen) {
				continue;
			}
			if (dx + dy <= MATCH_DISTANCE && (best < 0 || dx + dy < best_distance)) {
				best = (int)id;
				best_distance = dx + dy;
			}
		}
		if (best < 0) {
			for (unsigned id = 0; id < HIMAX_MAX_CONTACTS; id++) {
				if (!contact[id].down) {
					best = (int)id;
					break;
				}
			}
			if (best < 0) {
				continue;                         /* more fingers than numbers to give them */
			}
		}
		/*
		 * gk3.dts: touchscreen-inverted-x first, then touchscreen-swapped-x-y; its own
		 * comment says the two do not commute, so the order is as written. Invert the
		 * sensor's x (sx' = 25600 - sx), then take the panel from the swapped pair:
		 * panel_x = sy * 1600 / 16000, panel_y = sx' * 2560 / 25600.
		 */
		uint32_t panel_x = (uint32_t)(((uint64_t)y * PANEL_X) / Y_TOTAL);
		uint32_t panel_y = (uint32_t)(((uint64_t)(X_TOTAL - x) * PANEL_Y) / X_TOTAL);

		seen.touches++;
		seen.last_x = panel_x;
		seen.last_y = panel_y;
		if (shown++ < 40) {
			printf("himax: finger %u at %u, %u (of %u x %u)\n", best, panel_x, panel_y, PANEL_X, PANEL_Y);
		}
		contact[best].seen = true;
		contact[best].sx = x;
		contact[best].sy = y;
		if (!contact[best].down) {
			contact[best].down = true;
			hakios_touch_post(HAKIOS_TOUCH_DOWN, best, panel_x, panel_y);
		} else if (contact[best].x != panel_x || contact[best].y != panel_y) {
			hakios_touch_post(HAKIOS_TOUCH_MOVE, best, panel_x, panel_y);
		}
		contact[best].x = panel_x;
		contact[best].y = panel_y;
	}
	/* a finger that is no longer in the frame has lifted */
	for (unsigned id = 0; id < HIMAX_MAX_CONTACTS; id++) {
		if (contact[id].down && !contact[id].seen) {
			contact[id].down = false;
			hakios_touch_post(HAKIOS_TOUCH_UP, id, contact[id].x, contact[id].y);
		}
	}
}

/* Burst-read the frame and turn its peaks into fingers. */
static void
himax_poll(void)
{
	struct himax_peak peaks[HIMAX_MAX_PEAKS];
	uint32_t sx[HIMAX_MAX_CONTACTS], sy[HIMAX_MAX_CONTACTS];
	unsigned peaks_count = 0, count = 0;
	uint32_t total = HIMAX_BUS_R_HLEN + HIMAX_REPORT_BYTES;

	seen.polls++;
	if (!himax_set_burst_mode(true)) {
		goto bad;
	}
	/* one burst: the chip starts at the event stack and auto-adds four per beat (the vendor's own chunked read) */
	memset(himax_rx, 0, total);
	himax_tx[0] = HIMAX_FUNCTION_READ;
	himax_tx[1] = HIMAX_AHB_ADDR_EVENT_STACK;
	himax_tx[2] = 0x00;
	if (!spi_transfer(himax_tx, himax_rx, total)) {
		goto bad;
	}
	seen.reports++;
	for (unsigned i = 0; i < HIMAX_MAX_CONTACTS; i++) {
		contact[i].seen = false;
	}
	if (himax_any_touch()) {
		himax_find_peaks(peaks, &peaks_count);
		for (unsigned i = 0; i < peaks_count && count < HIMAX_MAX_CONTACTS; i++) {
			if (himax_centroid(&peaks[i], &sx[count], &sy[count])) {
				count++;
			}
		}
	}
	himax_report(count, sx, sy);
	return;
bad:
	seen.bad++;
	if (seen.bad <= 8) {
		printf("himax: the frame read failed (%u so far)\n", seen.bad);
	}
}

/* ---- all of it together --------------------------------------------------------------------------- */

enum himax_stage {
	STAGE_CX = 1, STAGE_CLOCKS = 2, STAGE_PINS = 3, STAGE_CHIP = 4, STAGE_REPORTS = 5
};
static unsigned stop_after = STAGE_REPORTS;

static void
show_state(const char *when)
{
	printf("himax: %s: votes %#x, branches ahb-m %#x ahb-s %#x se6 %#x, selector cmd %#x cfg %#x; "
	    "pins cs/clk/mosi/miso cfg %#x %#x %#x %#x, reset %#x/%u, int %#x/%u\n", when, rd(GCC_VOTE),
	    rd(GCC_QUP0_M_AHB_CBCR), rd(GCC_QUP0_S_AHB_CBCR), rd(GCC_QUP0_SE6_CBCR), rd(GCC_QUP0_SE6_CMD),
	    rd(GCC_QUP0_SE6_CFG), rd(GPIO_CFG(PIN_CS)), rd(GPIO_CFG(PIN_CLK)), rd(GPIO_CFG(PIN_MOSI)),
	    rd(GPIO_CFG(PIN_MISO)), rd(GPIO_CFG(PIN_RESET)), rd(GPIO_IO(PIN_RESET)), rd(GPIO_CFG(PIN_INT)),
	    rd(GPIO_IO(PIN_INT)));
}

static void
use_engine_pins(void)
{
	printf("himax: pins 154-157 to function %d: that is the device tree's \"qup6\" as a number; the phone's driver "
	    "wrote the same 1 for its \"qup15\" group. 2 mA drive and pull-downs, as its spi6_default state says\n",
	    PIN_FUNCTION_QUP);
	gpio_configure(PIN_MISO, PIN_FUNCTION_QUP, PULL_DOWN, 2, false);
	gpio_configure(PIN_MOSI, PIN_FUNCTION_QUP, PULL_DOWN, 2, false);
	gpio_configure(PIN_CLK, PIN_FUNCTION_QUP, PULL_DOWN, 2, false);
	gpio_configure(PIN_CS, PIN_FUNCTION_QUP, PULL_DOWN, 2, false);
}

/* How far bring_up got: the standing-by line says it, because the print that told us scrolled
 * away long before anyone could read it (a real machine starts this in under a second). */
static unsigned reached;

/* -1: something failed; 0: asked to stop at a stage, nothing to complain about; 1: ready. */
static int
bring_up(void)
{
	const char *problem;

	printf("himax: stage 1 of 5: switching the CX domain on (this SoC's QUP and clock controller hang off it)\n");
	if (!cx_domain_on()) {
		printf("himax: the CX domain is not asked for; the chip is left alone\n");
		return -1;
	}
	reached = STAGE_CX;
	if (stop_after == STAGE_CX) {
		printf("himax: himax_stop=%u: stopping after stage 1\n", stop_after);
		return 0;
	}

	printf("himax: stage 2 of 5: turning on serial engine 6's clocks and the engine itself\n");
	show_state("as found");
	if (!clocks_on()) {
		printf("himax: the engine's clocks cannot be turned on\n");
		return -1;
	}
	problem = geni_spi_init(12000000);              /* the device tree's spi-max-frequency, as a ceiling */
	if (problem != NULL) {
		printf("himax: %s\n", problem);
		return -1;
	}
	spi_transfer = geni_transfer;
	reached = STAGE_CLOCKS;
	if (stop_after == STAGE_CLOCKS) {
		printf("himax: himax_stop=%u: stopping after stage 2\n", stop_after);
		return 0;
	}

	printf("himax: stage 3 of 5: the pins\n");
	gpio_output(PIN_RESET, true, PULL_NONE, 2);     /* released: the device tree's active-low means idle is high */
	gpio_configure(PIN_INT, 0, PULL_NONE, 2, false);   /* the device tree leaves this one unbiased */
	use_engine_pins();
	show_state("pins set");
	reached = STAGE_PINS;
	if (stop_after == STAGE_PINS) {
		printf("himax: himax_stop=%u: stopping after stage 3\n", stop_after);
		return 0;
	}

	printf("himax: stage 4 of 5: reset (drive low 5 ms, release, 60 ms while the chip re-reads its flash) "
	    "and identification\n");
	himax_pin_reset();
	if (!himax_probe()) {
		return -1;
	}
	reached = STAGE_CHIP;
	if (stop_after == STAGE_CHIP) {
		printf("himax: himax_stop=%u: stopping after stage 4\n", stop_after);
		return 0;
	}

	printf("himax: stage 5 of 5: the reports\n");
	if (!himax_raw_frames()) {
		return -1;
	}
	return 1;
}

static void
himax_thread(__unused void *parameter, __unused wait_result_t result)
{
	unsigned idle = 0, idle_looks;
	uint32_t poll_us = 8000;

	PE_parse_boot_argn("touch_poll_us", &poll_us, sizeof(poll_us));
	if (poll_us < 250 || poll_us > 100000) {
		poll_us = 8000;
	}
	idle_looks = 80000 / poll_us;   /* a frame read every 80 ms even with the line high, in case it is missed */

	tlmm = ml_io_map(TLMM_PHYS, TLMM_SIZE);
	gcc = ml_io_map(GCC_PHYS, GCC_SIZE);
	se = ml_io_map(SE_PHYS, SE_SIZE);
	rsc = ml_io_map(RSC_PHYS, RSC_SIZE);
	cmd_db = ml_io_map_wcomb(CMD_DB_PHYS, CMD_DB_SIZE);      /* memory, not registers: no alignment rules */

	int up = bring_up();
	if (up < 0) {
		if (stop_after >= STAGE_REPORTS) {
			printf("himax: no himax touch screen\n");
		}
		/* Standing by is the point: on this machine the screen is the instrument, the
		 * boot prints scroll away in under a second, and a reader with a photo of the
		 * desktop cannot reach what went past. Every half minute, say where things
		 * stopped and what the lines read now; touch nothing else. */
		for (;;) {
			sleep_us(30 * 1000 * 1000);
			printf("himax: standing by: it got as far as stage %u of 5; the interrupt line is %s, "
			    "the reset line is %s; the engine's clock branch reads %#x, the votes %#x\n",
			    reached > STAGE_REPORTS ? STAGE_REPORTS : reached,
			    gpio_get(PIN_INT) ? "high" : "low",
			    gpio_get(PIN_RESET) ? "released (high, as it should be)" : "held low (the chip is in reset)",
			    rd(GCC_QUP0_SE6_CBCR), rd(GCC_VOTE));
		}
	}
	if (up == 0) {
		/* a deliberate himax_stop: the probe asked to stop at a stage; nothing to complain about */
		thread_terminate(current_thread());
		return;
	}
	printf("himax: himax touch screen ready; the interrupt line is looked at every %u us\n", poll_us);
	for (unsigned tick = 1;; tick++) {
		/* the interrupt line is low while a frame waits; look anyway every 80 ms in case it is missed */
		if (!gpio_get(PIN_INT)) {
			idle = 0;
			himax_poll();
		} else if (++idle >= idle_looks) {
			idle = 0;
			himax_poll();
		}
		if (tick % (30 * (1000000 / poll_us)) == 0) {
			printf("himax: so far %u frames in %u looks (%u bad), %u finger positions, the last at %u, %u; "
			    "interrupt line %s; the line looked at every %u us\n",
			    seen.reports, seen.polls, seen.bad, seen.touches, seen.last_x, seen.last_y,
			    gpio_get(PIN_INT) ? "high" : "low", poll_us);
		}
		sleep_us(poll_us);
	}
}

void
hakios_himax_init(void)
{
	uint32_t enabled = 0, stop = STAGE_REPORTS;
	thread_t thread;

	if (!PE_parse_boot_argn("touchscreen", &enabled, sizeof(enabled)) || !enabled) {
		return;
	}
	printf("himax: the touch screen of the Matebook E Go: a Himax HX83121A on serial engine 6 of the first wrapper\n");
	if (PE_parse_boot_argn("himax_stop", &stop, sizeof(stop))) {
		stop_after = stop > STAGE_REPORTS ? STAGE_REPORTS : stop;
		printf("himax: himax_stop=%u: the stages are 1 the CX domain, 2 the clocks and the engine, 3 the pins, "
		    "4 reset and chip ID, 5 the reports\n", stop_after);
		if (stop_after == 0) {
			printf("himax: himax_stop=0: no register is touched\n");
			return;
		}
	}
	if (kernel_thread_start(himax_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
