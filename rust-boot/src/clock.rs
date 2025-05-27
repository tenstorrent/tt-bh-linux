use luwen_if::chip::Blackhole;
use luwen_if::chip::HlComms;
use std::{thread, time::Duration};

const PLL4_BASE: u64 = 0x80020500; // PLL4 is for L2CPU
const PLL_CNTL_1: u64 = 0x4;
const PLL_CNTL_5: u64 = 0x14;

#[derive(Debug)]
pub struct PLLCNTL5 {
    postdiv: [u8; 4],
}

impl PLLCNTL5 {
    pub fn from_bytes(data: &[u8]) -> Self {
        let mut postdiv = [0u8; 4];
        postdiv.copy_from_slice(&data[..4]);
        Self { postdiv }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        self.postdiv.to_vec()
    }

    pub fn step(
        &mut self,
        chip: &Blackhole,
        target: u8,
        field: usize,
    ) -> Result<(), Box<dyn std::error::Error>> {
        while self.postdiv[field] != target {
            let one_step = if target > self.postdiv[field] { 1i8 } else { -1i8 };
            self.postdiv[field] = (self.postdiv[field] as i8 + one_step) as u8;
            chip.axi_write(PLL4_BASE + PLL_CNTL_5, &self.to_bytes())?;
            thread::sleep(Duration::from_nanos(1));
        }
        Ok(())
    }
}

#[derive(Debug)]
pub struct PLLCNTL1 {
    refdiv: u8,
    postdiv: u8,
    fbdiv: u16,
}

impl PLLCNTL1 {
    pub fn from_bytes(data: &[u8]) -> Self {
        Self { refdiv: data[0], postdiv: data[1], fbdiv: u16::from_le_bytes([data[2], data[3]]) }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(4);
        bytes.push(self.refdiv);
        bytes.push(self.postdiv);
        bytes.extend_from_slice(&self.fbdiv.to_le_bytes());
        bytes
    }

    pub fn step_fbdiv(
        &mut self,
        chip: &Blackhole,
        target: u16,
    ) -> Result<(), Box<dyn std::error::Error>> {
        while self.fbdiv != target {
            let one_step = if target > self.fbdiv { 1i16 } else { -1i16 };
            self.fbdiv = (self.fbdiv as i16 + one_step) as u16;
            chip.axi_write(PLL4_BASE + PLL_CNTL_1, &self.to_bytes())?;
            thread::sleep(Duration::from_nanos(1));
        }
        Ok(())
    }
}

pub fn set_l2cpu_pll(chip: &Blackhole, mhz: u32) -> Result<(), Box<dyn std::error::Error>> {
    let (sol_fbdiv, sol_postdivs) = match mhz {
        200 => (128, [15, 15, 15, 15]),
        1750 => (140, [1, 1, 1, 1]),
        _ => return Err(format!("Unsupported frequency: {} MHz", mhz).into()),
    };

    // Read initial post dividers
    let mut initial_post_dividers = vec![0, 0, 0, 0];
    chip.axi_read(PLL4_BASE + PLL_CNTL_5, &mut initial_post_dividers)?;
    let mut initial_post_dividers = PLLCNTL5::from_bytes(&initial_post_dividers);

    // Read initial fbdiv
    let mut initial_fbdiv_bytes = vec![0, 0, 0, 0];
    chip.axi_read(PLL4_BASE + PLL_CNTL_1, &mut initial_fbdiv_bytes)?;
    let mut initial_fbdiv = PLLCNTL1::from_bytes(&initial_fbdiv_bytes);

    // Calculate which post dividers need to increase/decrease
    let mut increasing_postdivs = Vec::new();
    let mut decreasing_postdivs = Vec::new();

    for (idx, &target) in sol_postdivs.iter().enumerate() {
        if target > initial_post_dividers.postdiv[idx] {
            increasing_postdivs.push((idx, target));
        } else if target < initial_post_dividers.postdiv[idx] {
            decreasing_postdivs.push((idx, target));
        }
    }

    for (idx, target) in increasing_postdivs {
        initial_post_dividers.step(chip, target, idx)?;
    }

    initial_fbdiv.step_fbdiv(chip, sol_fbdiv)?;

    for (idx, target) in decreasing_postdivs {
        initial_post_dividers.step(chip, target, idx)?;
    }

    Ok(())
}
