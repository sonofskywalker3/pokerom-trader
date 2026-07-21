-- Headless verification: inject our edited Emerald save into SRAM, boot to
-- Continue, then read the LIVE gSaveBlock1 flags the running game loaded.
local SAV  = "C:/Users/Jeff/Documents/Projects/Pokemon/roms/Pokemon - Emerald Version (USA, Europe).sav"
local OUT  = "C:/Users/Jeff/Documents/Projects/Pokemon/emu/verify_out.txt"

-- Emerald (US) symbols / offsets
local GSAVEBLOCK1PTR = 0x03005D8C  -- pointer to SaveBlock1 in EWRAM
local FLAGS_OFF      = 0x1270      -- SaveBlock1.flags[]
local FLAG_FARAWAY   = 0x8D6       -- FLAG_ENABLE_SHIP_FARAWAY_ISLAND (Old Sea Map)
local FLAG_GAMECLEAR = 0x864       -- FLAG_SYS_GAME_CLEAR (postgame marker)

local out = io.open(OUT, "w")
local function log(s) out:write(s .. "\n"); out:flush() end

log("system: " .. tostring(emu.getsystemid()))
-- BizHawk loads our edited save from GBA/SaveRAM/<game>.SaveRAM natively.

-- Boot: mash Start then A to skip intro -> title -> Continue -> load.
for i = 1, 240 do joypad.set({ Start = true }, 1); emu.frameadvance() end
for i = 1, 360 do joypad.set({ A = true }, 1); emu.frameadvance() end
for i = 1, 240 do emu.frameadvance() end  -- let the save load settle

-- Read live gSaveBlock1 flags.
memory.usememorydomain("System Bus")
local sb1 = memory.read_u32_le(GSAVEBLOCK1PTR)
log(string.format("gSaveBlock1Ptr = 0x%08X", sb1))

local function read_flag(id)
  local addr = sb1 + FLAGS_OFF + math.floor(id / 8)
  local b = memory.read_u8(addr)
  return math.floor(b / (2 ^ (id % 8))) % 2
end

local FLAGS = {
  { "GAME_CLEAR         (0x864)", 0x864 },
  { "EON/Southern       (0x8B3)", 0x8B3 },
  { "MYSTIC/Navel Rock  (0x8E0)", 0x8E0 },
  { "AURORA/Birth Isl   (0x8D5)", 0x8D5 },
  { "OLDSEAMAP/Faraway  (0x8D6)", 0x8D6 },
}

if sb1 >= 0x02000000 and sb1 < 0x02040000 then
  for _, e in ipairs(FLAGS) do
    log(e[1] .. " live = " .. (read_flag(e[2]) == 1 and "SET" or "clear"))
  end
  log("RESULT: verification read complete")
else
  log("SaveBlock1 not loaded yet (pointer invalid) - navigation/timing needs adjusting")
end

out:close()
client.exit()
