use clap::Parser;
use clap_num::maybe_hex;
use luwen_if::chip::HlComms;
use luwen_if::{chip::Blackhole, ChipImpl};
use std::{fs::File, io::Read, thread::sleep, time::Duration};

mod clock;

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// Boot the core after loading the bin files
    #[arg(long)]
    boot: bool,

    /// List of L2CPUs to boot
    #[arg(long, num_args = 1.., default_values_t = [0])]
    l2cpu: Vec<u32>,

    /// Path to rootfs bin file
    #[arg(long)]
    rootfs_bin: String,

    /// List of Destination address for rootfs for each l2cpu
    #[arg(long, num_args = 1.., value_parser = maybe_hex::<u64>)]
    rootfs_dst: Vec<u64>,

    /// Path to opensbi bin file
    #[arg(long)]
    opensbi_bin: String,

    /// List of Destination address for opensbi for each l2cpu
    #[arg(long, num_args = 1.., value_parser = maybe_hex::<u64>)]
    opensbi_dst: Vec<u64>,

    /// Path to kernel bin file
    #[arg(long)]
    kernel_bin: Option<String>,

    /// List of Destination addresses for kernel for each l2cpu
    #[arg(long, num_args = 1.., value_parser = maybe_hex::<u64>)]
    kernel_dst: Option<Vec<u64>>,

    /// List of path to dtb bin file for each l2cpu
    #[arg(long, num_args = 1..)]
    dtb_bin: Option<Vec<String>>,

    /// List of Destination address for dtb for each l2cpu
    #[arg(long, num_args = 1.., value_parser = maybe_hex::<u64>)]
    dtb_dst: Option<Vec<u64>>,
}

// Constants from the Python script
const L2CPU_TILE_MAPPING: [(u8, u8); 4] = [
    (8, 3), // L2CPU0
    (8, 9), // L2CPU1
    (8, 5), // L2CPU2
    (8, 7), // L2CPU3
];

const L2CPU_GDDR_ENABLE_BIT_MAPPING: [u8; 4] = [
    5, // L2CPU0 is attached to tt_gddr6_ss_even_inst[2]
    6, // L2CPU1 is attached to tt_gddr6_ss_odd_inst[3]
    7, // L2CPU2 is attached to tt_gddr6_ss_even_inst[3]
    7, // L2CPU3 is attached to tt_gddr6_ss_even_inst[3]
];

fn read_bin_file(file_path: &str) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    let mut file = File::open(file_path)?;
    let mut buffer = Vec::new();
    file.read_to_end(&mut buffer)?;

    // Calculate padding
    let padding = buffer.len() % 4;
    if padding != 0 {
        let padding_bytes_needed = 4 - padding;
        buffer.extend(vec![0; padding_bytes_needed]);
    }

    Ok(buffer)
}

fn reset_x280(chip: &Blackhole, l2cpu_indices: &[u32]) -> Result<(), Box<dyn std::error::Error>> {
    const RESET_UNIT_BASE: u64 = 0x80030000;

    clock::set_l2cpu_pll(chip, 200)?;

    let mut l2cpu_reset_val = chip.axi_read32(RESET_UNIT_BASE + 0x14)?;
    for &l2cpu_index in l2cpu_indices {
        l2cpu_reset_val |= 1 << (l2cpu_index + 4);
    }
    chip.axi_write32(RESET_UNIT_BASE + 0x14, l2cpu_reset_val)?;
    chip.axi_read32(RESET_UNIT_BASE + 0x14)?;

    clock::set_l2cpu_pll(chip, 1750)?;

    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();

    for &l2cpu in &args.l2cpu {
        if l2cpu >= 4 {
            return Err("l2cpu IDs must be in [0, 1, 2, 3]".into());
        }
    }

    // Validate arguments
    let l2cpu_count = args.l2cpu.len();
    let len = args.rootfs_dst.len();
    if l2cpu_count != len {
        return Err(format!("Expected {} rootfs-dst arguments, found {}", l2cpu_count, len).into());
    }

    let len = args.opensbi_dst.len();
    if l2cpu_count != len {
        return Err(format!("Expected {} opensbi-dst arguments, found {}", l2cpu_count, len).into());
    }

    if let Some(kernel_dst) = &args.kernel_dst {
        let len = kernel_dst.len();
        if l2cpu_count != len {
            return Err(
                format!("Expected {} kernel-dst arguments, found {}", l2cpu_count, len).into()
            );
        }
    }

    if let Some(dtb_dst) = &args.dtb_dst {
        let len = dtb_dst.len();
        if l2cpu_count != len {
            return Err(format!("Expected {} dtb-dst arguments, found {}", l2cpu_count, len).into());
        }
    }

    if let Some(dtb_bin) = &args.dtb_bin {
        let len = dtb_bin.len();
        if l2cpu_count != len {
            return Err(format!("Expected {} dtb-bin arguments, found {}", l2cpu_count, len).into());
        }
    }

    let chips = luwen_ref::detect_chips()?;
    let chip = chips
        .iter()
        .filter_map(|c| c.as_bh())
        .take(1)
        .next()
        .ok_or("No chips found".to_string())?;

    // Sleep 1s, telemetry sometimes not available immediately after reset
    sleep(Duration::from_secs(1));

    let telemetry = chip.get_telemetry()?;
    let enabled_l2cpu = telemetry.enabled_l2cpu;
    let enabled_gddr = telemetry.enabled_gddr;

    for &l2cpu in &args.l2cpu {
        if enabled_l2cpu & (1 << l2cpu) == 0 {
            return Err(format!("L2CPU {} is harvested", l2cpu).into());
        }
        if enabled_gddr & (1 << L2CPU_GDDR_ENABLE_BIT_MAPPING[l2cpu as usize]) == 0 {
            return Err(format!("DRAM attached to L2CPU {} is harvested", l2cpu).into());
        }
    }

    for (i, &l2cpu) in args.l2cpu.iter().enumerate() {
        let (l2cpu_noc_x, l2cpu_noc_y) = L2CPU_TILE_MAPPING[l2cpu as usize];
        let l2cpu_base = 0xfffff7fefff10000;

        let opensbi_addr = args.opensbi_dst[i];
        let rootfs_addr = args.rootfs_dst[i];
        let opensbi_bytes = read_bin_file(&args.opensbi_bin)?;
        let rootfs_bytes = read_bin_file(&args.rootfs_bin)?;

        let mut kernel_addr = None;
        let mut kernel_bytes = None;
        if let (Some(kernel_bin), Some(kernel_dst)) = (&args.kernel_bin, &args.kernel_dst) {
            kernel_addr = Some(kernel_dst[i]);
            kernel_bytes = Some(read_bin_file(kernel_bin)?);
        }

        let mut dtb_addr = None;
        let mut dtb_bytes = None;
        if let (Some(dtb_bin), Some(dtb_dst)) = (&args.dtb_bin, &args.dtb_dst) {
            dtb_addr = Some(dtb_dst[i]);
            dtb_bytes = Some(read_bin_file(&dtb_bin[i])?);
        }

        // Enable the whole cache when using DRAM
        const L3_REG_BASE: u64 = 0x02010000;
        chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, L3_REG_BASE + 8, 0xf)?;
        let _data = chip.noc_read32(0, l2cpu_noc_x, l2cpu_noc_y, L3_REG_BASE + 8)?;

        println!("Writing {} bytes of OpenSBI to 0x{:x}", opensbi_bytes.len(), opensbi_addr);
        chip.noc_write(0, l2cpu_noc_x, l2cpu_noc_y, opensbi_addr, &opensbi_bytes)?;
        println!("Writing {} bytes of rootfs to 0x{:x}", rootfs_bytes.len(), rootfs_addr);
        chip.noc_write(0, l2cpu_noc_x, l2cpu_noc_y, rootfs_addr, &rootfs_bytes)?;

        if let (Some(addr), Some(bytes)) = (kernel_addr, kernel_bytes.as_ref()) {
            println!("Writing {} bytes of Kernel to 0x{:x}", bytes.len(), addr);
            chip.noc_write(0, l2cpu_noc_x, l2cpu_noc_y, addr, bytes)?;
        }

        if let (Some(addr), Some(bytes)) = (dtb_addr, dtb_bytes.as_ref()) {
            println!("Writing {} bytes of dtb to 0x{:x}", bytes.len(), addr);
            chip.noc_write(0, l2cpu_noc_x, l2cpu_noc_y, addr, bytes)?;
        }

        // Write reset vectors
        for offset in 0..4 {
            chip.noc_write32(
                0,
                l2cpu_noc_x,
                l2cpu_noc_y,
                l2cpu_base + offset * 8,
                (opensbi_addr & 0xffffffff) as u32,
            )?;
            chip.noc_write32(
                0,
                l2cpu_noc_x,
                l2cpu_noc_y,
                l2cpu_base + offset * 8 + 4,
                (opensbi_addr >> 32) as u32,
            )?;
        }
    }

    if args.boot {
        reset_x280(&chip, &args.l2cpu)?;
    } else {
        println!("Not booting (you didn't pass --boot)");
    }

    // Configure L2 prefetchers
    for &l2cpu in &args.l2cpu {
        let (l2cpu_noc_x, l2cpu_noc_y) = L2CPU_TILE_MAPPING[l2cpu as usize];
        const L2_PREFETCH_BASE: u64 = 0x02030000;
        for offset in [0x0000, 0x2000, 0x4000, 0x6000] {
            chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, L2_PREFETCH_BASE + offset, 0x15811)?;
            chip.noc_write32(0, l2cpu_noc_x, l2cpu_noc_y, L2_PREFETCH_BASE + offset + 4, 0x38c84e)?;
        }
    }

    Ok(())
}
