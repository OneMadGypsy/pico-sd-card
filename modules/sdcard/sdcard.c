#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "pico/time.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "extmod/vfs.h"
#include <math.h>
#include <string.h>

#define SPI_BAUDRATE    (100000) //this is only the initialization baudrate
#define SPI_POLARITY    (0)
#define SPI_PHASE       (0)
#define SPI_BITS        (8)
#define SPI_FIRSTBIT    (1)

#define IOCTL_INIT      (1)
#define IOCTL_DEINIT    (2)
#define IOCTL_SYNC      (3)
#define IOCTL_BLK_COUNT (4)
#define IOCTL_BLK_SIZE  (5)
#define IOCTL_BLK_ERASE (6)

#define CMD0            (0x40) // CMD0 : init card; should return _IDLE_STATE
#define CMD8            (0x48) // CMD8 : determine card version
#define CMD9            (0x49) // CMD9 : response R2 (R1 byte + 16-byte block read)
#define CMD12           (0x4C) // CMD12: forces card to stop transmission in Multiple Block Read Operation
#define CMD16           (0x50) // CMD16: set block length to 512 bytes
#define CMD17           (0x51) // CMD17: set read address for single block
#define CMD18           (0x52) // CMD18: set read address for multiple blocks
#define CMD24           (0x58) // CMD24: set write address for single block
#define CMD25           (0x59) // CMD25: set write address for first block
#define CMD41           (0x69) // CMD41: host capacity support information / activates card's initialization process.
#define CMD55           (0x77) // CMD55: next command is app command
#define CMD58           (0x7a) // CMD58: read OCR register. CCS bit is assigned to OCR[30]

#define BLOCK           (0x200) //512
#define FF              (uint8_t []){0xFF}

#define CMD_TIMEOUT     (0x64)  //100

#define IDLE_STATE      (0x01)
#define ERASE_RESET     (0x02)
#define ILLEGAL_CMD     (0x04)
#define COM_CRC_ERROR   (0x08)
#define ERASESEQ_ERROR  (0x10)
#define ADDR_ERROR      (0x20)
#define PARAM_ERROR     (0x40)

#define TOKEN_CMD25     (0xFC)
#define TOKEN_STOP_TRAN (0xFD)
#define TOKEN_DATA      (0xFE)


//__> BUFFER SLICE _________________________________________________________________
typedef struct{ 
    uint8_t buff[BLOCK];
    int len;
} slice_obj_t;

slice_obj_t slice(const uint8_t* buff, int size, int start, int end) {
    slice_obj_t result = { .buff = {0}, .len = 0 };
    if (start >= 0 && end <= size) {
        int count = 0;
        for (int i = start; i < end; i++) {
            result.buff[count] = buff[i];
            count++;
        }
        result.len = end - start;
        return result;
    }
    else {
        mp_raise_msg(&mp_type_IndexError, MP_ERROR_TEXT("index is out of range"));
        result.len = -1;
        return result;
    }
} 


//__> SDObject __________________________________________________________________________
const mp_obj_type_t sdcard_SDObject_type;

typedef struct _sdcard_SDObject_obj_t {
    mp_obj_base_t base;
    spi_inst_t    *spi;
    const char *type;
    uint8_t   buffer[BLOCK];
    uint8_t   token[1];
    uint64_t  sectors;
    uint32_t  baudrate;
    uint8_t   cs;
    uint16_t  cdv;
} sdcard_SDObject_obj_t;

STATIC void SDObject_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind; (void)self_in;
    //sdcard_SDObject_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "SDObject()");
}

//used for defaulting cmd arguments
typedef struct {
    sdcard_SDObject_obj_t *self; 
    uint8_t cmd; 
    uint32_t arg; 
    uint8_t crc;
    uint8_t final; 
    bool hold; 
    bool skip;
} cmd_args;

//actual cmd logic
STATIC int sdcard_cmd_base(sdcard_SDObject_obj_t *self, uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t final, bool hold, bool skip) {
    //mp_printf(MP_PYTHON_PRINTER, "cmd %u, arg %u, crc %u, final %u, hold %b, skip %b\n", cmd, arg, crc, final, hold, skip);
    
    gpio_put(self->cs, 0);
    uint8_t cmd_stream[6] = {cmd, ((arg >> 24) & 0xFF), ((arg >> 16) & 0xFF), ((arg >> 8) & 0xFF), (arg & 0xFF), crc};
    
    spi_write_blocking(self->spi, cmd_stream, 6);
    
    if (skip) spi_read_blocking(self->spi, 0xFF, self->token, 1);
    
    for (int i=0; i<CMD_TIMEOUT; i++){
        spi_read_blocking(self->spi, 0xFF, self->token, 1);
        if((self->token[0] & 0x80) == 0x00){
            for(int j=0; j<final; j++) spi_write_blocking(self->spi, FF, 1);
            if (!hold){
                gpio_put(self->cs, 1);
                spi_write_blocking(self->spi, FF, 1);
            }
            return self->token[0];
        }
    }
    
    gpio_put(self->cs, 1);
    spi_write_blocking(self->spi, FF, 1);
    return -1;
}

//defaults cmd arguments
STATIC int var_cmd_args(cmd_args in){
    sdcard_SDObject_obj_t *self = in.self; 
    uint8_t cmd               = in.cmd; 
    uint32_t arg              = in.arg   ? in.arg   : 0x00000000; 
    uint8_t crc               = in.crc   ? in.crc   : 0x00;
    uint8_t final             = in.final ? in.final : 0; 
    bool hold                 = in.hold  ? in.hold  : false; //I had to reverse the concept here
    bool skip                 = in.skip  ? in.skip  : false;
    return sdcard_cmd_base(self, cmd, arg, crc, final, hold, skip);
}

//proxy that applies default arguments
#define sdcard_cmd(...) var_cmd_args((cmd_args){__VA_ARGS__})

STATIC void sdcard_init_v1(sdcard_SDObject_obj_t *self) {
    for (int i=0; i<CMD_TIMEOUT; i++){
        sdcard_cmd(self, CMD55);
        if (sdcard_cmd(self, CMD41) == 0){
            self->cdv = BLOCK;
            self->type = "SD Card v1";
            return;
        }
    }
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Timeout V1"));
}

STATIC void sdcard_init_v2(sdcard_SDObject_obj_t *self) {
    for (int i=0; i<CMD_TIMEOUT; i++){
        sleep_ms(50);
        sdcard_cmd(self, CMD58, .final=4);
        sdcard_cmd(self, CMD55); //This is where the problems begin ~ but it is identical to the py version that works
        if (sdcard_cmd(self, CMD41, 0x40000000) == 0) {
            sdcard_cmd(self, CMD58, .final=4);
            self->cdv = 1;
            self->type = "SD Card v2";
            return;
        }
    }
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Timeout V2"));
}

STATIC void sdcard_readinto(sdcard_SDObject_obj_t *self, uint8_t *csd, int len) {
    gpio_put(self->cs, 0);
    
    bool check = false;
    
    for (int i=0; i<CMD_TIMEOUT; i++){
        spi_read_blocking(self->spi, 0xFF, self->token, 1);
        if (self->token[0] == TOKEN_DATA) {
            check = true;
            break;
        }
        sleep_ms(1);
    }
    
    if(!check) {
        gpio_put(self->cs, 1);
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Response Timeout"));
    }
    
    slice_obj_t b = slice(self->buffer, len, 0, len);
    
    spi_write_read_blocking(self->spi, b.buff, csd, len);
    
    spi_write_blocking(self->spi, FF, 1);
    spi_write_blocking(self->spi, FF, 1);
    gpio_put(self->cs, 1);
    spi_write_blocking(self->spi, FF, 1);
}

STATIC void sdcard_write_token(sdcard_SDObject_obj_t *self, uint8_t token){
    gpio_put(self->cs, 0);
    
    spi_read_blocking(self->spi, token, self->token, 1);
    spi_write_blocking(self->spi, FF, 1);
    
    // wait for write to finish
    spi_read_blocking(self->spi, 0xFF, self->token, 1);
    while (!self->token[0])
        spi_read_blocking(self->spi, 0xFF, self->token, 1);

    gpio_put(self->cs, 1);
    spi_write_blocking(self->spi, FF, 1);
}

STATIC void sdcard_write(sdcard_SDObject_obj_t *self, uint8_t token, uint8_t *buf, int len){
        gpio_put(self->cs, 0);

        // send: start of block, data, checksum
        spi_read_blocking(self->spi, token, self->token, 1);
        spi_write_blocking(self->spi, buf, len);
        spi_write_blocking(self->spi, FF, 1);
        spi_write_blocking(self->spi, FF, 1);

        // check the response
        spi_read_blocking(self->spi, 0xFF, self->token, 1);
        
        if ((self->token[0] & 0x1F) != 0x05) {
            gpio_put(self->cs, 1);
            spi_write_blocking(self->spi, FF, 1);
            return;
        }

        // wait for write to finish
        spi_read_blocking(self->spi, 0xFF, self->token, 1);
        while (!self->token[0])
            spi_read_blocking(self->spi, 0xFF, self->token, 1);

        gpio_put(self->cs, 1);
        spi_write_blocking(self->spi, FF, 1);
}

STATIC mp_obj_t SDObject_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 3, true);
    sdcard_SDObject_obj_t *self = m_new_obj(sdcard_SDObject_obj_t);
    self->base.type = &sdcard_SDObject_type;
    
    enum {ARG_spi, ARG_cs, ARG_baudrate};
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_spi       , MP_ARG_REQUIRED | MP_ARG_INT , {.u_int     = 0       }},
        { MP_QSTR_cs        , MP_ARG_REQUIRED | MP_ARG_INT , {.u_int     = 0       }},
        { MP_QSTR_baudrate  , MP_ARG_INT                   , {.u_int     = 0x500000}},
    }; 
    
    mp_arg_val_t kw[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, kw);
    
    self->spi = (kw[ARG_spi].u_int == 0)? spi0 : spi1;
    spi_init(self->spi, SPI_BAUDRATE);
    spi_set_format(self->spi, SPI_BITS, SPI_POLARITY, SPI_PHASE, SPI_FIRSTBIT);
    
    self->token[0] = 0x00;
    for(int i=0; i<BLOCK; i++) self->buffer[i] = 0xFF;

    //setup chip-select pin
    self->cs  = kw[ARG_cs].u_int;
    gpio_set_function(self->cs, GPIO_FUNC_SIO);
    gpio_set_dir(self->cs, GPIO_OUT);
    gpio_put(self->cs, 1);
    
    for(int i=0; i < 16; i++) spi_write_blocking(self->spi, FF, 1);
    
    bool found = false;
    for (int i=0; i < 5; i++) {
        if (sdcard_cmd(self, CMD0, 0, 0x95) == IDLE_STATE) {
            found = true;
            break;
        }
    }
    
    if (!found) mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("No Card")); 
    
    int r1 = sdcard_cmd(self, CMD8, 0x0001AA, 0x87, 4);
    
    if      (r1 == IDLE_STATE)                  sdcard_init_v2(self);
    else if (r1 == (IDLE_STATE | ILLEGAL_CMD))  sdcard_init_v1(self);
    else mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Unknown Version"));
    
    if (sdcard_cmd(self, CMD9, .hold=true)) mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("No Response"));
            
    uint8_t csd[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    sdcard_readinto(self, csd, 16);
    
    if ((csd[0] & 0xC0) == 0x40) self->sectors = ((csd[8] << 8 | csd[9]) + 1) * 1024;   // CSD version 2.0
    else if ((csd[0] & 0xC0) == 0x00) {                                                 // CSD version 1.0 (old, <=2GB)
        uint16_t c_size       = (csd[6] & 0x3) | (csd[7] << 2) | ((csd[8] & 0xC0) << 4);
        uint16_t c_size_mult  = ((csd[9] & 0x3) << 1) | (csd[10] >> 7);
        self->sectors = (c_size + 1) * pow(2, (c_size_mult + 2));
    }
    else
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("CSD Format Unsupported"));
        
     
    if (sdcard_cmd(self, CMD16, BLOCK) != 0)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Can't Set Block Size"));

    spi_set_baudrate(self->spi, kw[ARG_baudrate].u_int);
    
    //mp_printf(MP_PYTHON_PRINTER, "card ready\n");
        
    return MP_OBJ_FROM_PTR(self);
}


//__> READ BLOCKS _____________________________________________________________________________________
STATIC void sdcard_readblocks(sdcard_SDObject_obj_t *self, int blocknum, uint8_t *buf, int len) {
    // mp_printf(MP_PYTHON_PRINTER, "readblocks\n");
    uint64_t nblocks = len/BLOCK;
    if ((!nblocks) || (len%BLOCK)) 
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Invalid Buffer Length"));
    
    if (nblocks == 1) {
        if (sdcard_cmd(self, CMD17, blocknum*self->cdv, .hold=true)) {
            gpio_put(self->cs, 1);
            mp_raise_OSError(5);
        }
                
        sdcard_readinto(self, buf, BLOCK);
    }
    else {
        if (sdcard_cmd(self, CMD18, blocknum*self->cdv, .hold=true)) {
            gpio_put(self->cs, 1);
            mp_raise_OSError(5);
        }
            
        slice_obj_t b;
        for (int i=0; i<nblocks; i++) {
            b = slice(buf, len, BLOCK+(BLOCK*(i-1)), BLOCK+(BLOCK*i));
            sdcard_readinto(self, b.buff, BLOCK);
        }
            
        if (sdcard_cmd(self, CMD12, 0, 0xFF, .skip=true))
            mp_raise_OSError(5);
    }
    
}

STATIC mp_obj_t SDObject_readblocks(mp_obj_t self_in, mp_obj_t block_num, mp_obj_t buf) {
    sdcard_SDObject_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;

    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_WRITE);
    sdcard_readblocks(self, mp_obj_get_int(block_num), bufinfo.buf, bufinfo.len);
    return mp_const_true;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_3(SDObject_readblocks_obj, SDObject_readblocks);

//__> WRITE BLOCKS _____________________________________________________________________________________
STATIC void sdcard_writeblocks(sdcard_SDObject_obj_t *self, int blocknum, uint8_t *buf, int len) {
    //mp_printf(MP_PYTHON_PRINTER, "writeblocks\n");
    uint64_t nblocks = len/BLOCK;
    if ((!nblocks) || (len%BLOCK)) 
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Invalid Buffer Length"));
    
    if (nblocks == 1) {
        if (sdcard_cmd(self, CMD24, blocknum*self->cdv)) mp_raise_OSError(5);
                
        sdcard_write(self, TOKEN_DATA, buf, len);
    }
    else {
        if (sdcard_cmd(self, CMD25, blocknum*self->cdv)) mp_raise_OSError(5);
            
        slice_obj_t b;
        for (int i=0; i<nblocks; i++) {
            b = slice(buf, len, BLOCK+(BLOCK*(i-1)), BLOCK+(BLOCK*i));
            sdcard_write(self, TOKEN_CMD25, b.buff, BLOCK);
        }
            
        if (sdcard_cmd(self, CMD12, 0, 0xFF, .skip=true)) mp_raise_OSError(5);
            
        sdcard_write_token(self, TOKEN_STOP_TRAN);
    }
}

STATIC mp_obj_t SDObject_writeblocks(mp_obj_t self_in, mp_obj_t block_num, mp_obj_t buf) {
    sdcard_SDObject_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;

    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_READ);
    sdcard_writeblocks(self, mp_obj_get_int(block_num), bufinfo.buf, bufinfo.len);
    return mp_const_true;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_3(SDObject_writeblocks_obj, SDObject_writeblocks);

//__> IOCTL _____________________________________________________________________________________
STATIC mp_obj_t SDObject_ioctl(mp_obj_t self_in, mp_obj_t cmd_obj, mp_obj_t arg_obj) {
    sdcard_SDObject_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t cmd = mp_obj_get_int(cmd_obj);
    switch (cmd) {
        case IOCTL_INIT:
            return MP_OBJ_NEW_SMALL_INT(0); // success
        case IOCTL_DEINIT:
            return MP_OBJ_NEW_SMALL_INT(0); // success
        case IOCTL_SYNC:
            return MP_OBJ_NEW_SMALL_INT(0); // success
        case IOCTL_BLK_COUNT:
            return MP_OBJ_NEW_SMALL_INT(self->sectors/1024);
        case IOCTL_BLK_SIZE:
            return MP_OBJ_NEW_SMALL_INT(BLOCK);
        default:
            return MP_OBJ_NEW_SMALL_INT(-1); // error
    }
}

STATIC MP_DEFINE_CONST_FUN_OBJ_3(SDObject_ioctl_obj, SDObject_ioctl);


STATIC const mp_rom_map_elem_t SDObject_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_readblocks), MP_ROM_PTR(&SDObject_readblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeblocks), MP_ROM_PTR(&SDObject_writeblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_ioctl), MP_ROM_PTR(&SDObject_ioctl_obj) },
};

STATIC MP_DEFINE_CONST_DICT(SDObject_locals_dict, SDObject_locals_dict_table);

STATIC void SDObject_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    sdcard_SDObject_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] == MP_OBJ_NULL) {
        if (attr == MP_QSTR_writeblocks) {
            dest[0] = MP_OBJ_FROM_PTR(&SDObject_writeblocks_obj);
            dest[1] = self;
        }
        else if (attr == MP_QSTR_readblocks) {
            dest[0] = MP_OBJ_FROM_PTR(&SDObject_readblocks_obj);
            dest[1] = self;  
        }
        else if (attr == MP_QSTR_ioctl) {
            dest[0] = MP_OBJ_FROM_PTR(&SDObject_ioctl_obj);
            dest[1] = self;  
        }
        //  return;
    } 
}

const mp_obj_type_t sdcard_SDObject_type = {
    { &mp_type_type },
    .name        = MP_QSTR_SDObject,
    .print       = SDObject_print,
    .make_new    = SDObject_make_new,
    .locals_dict = (mp_obj_dict_t*)&SDObject_locals_dict,
    .attr        = SDObject_attr,
};


//__> SDCard _______________________________________________________________________________________
const mp_obj_type_t sdcard_SDCard_type;

typedef struct _sdcard_SDCard_obj_t {
    mp_obj_base_t base;
    const char *drive;
    sdcard_SDObject_obj_t *sdobject;
} sdcard_SDCard_obj_t;

STATIC void SDCard_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind; (void)self_in;
    sdcard_SDCard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "SDCard(drive: %s, size mb: %lu, type: %s)", self->drive, self->sdobject->sectors/2048, self->sdobject->type);
}

//__> MOUNT ___________________________________________________________
STATIC mp_obj_t SDCard_mount(mp_obj_t self_in) {
    sdcard_SDCard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t d  = mp_obj_new_str(self->drive, strlen(self->drive));
    mp_obj_t mnt_args[2];
    mnt_args[0] = self->sdobject;
    mnt_args[1] = d;
    mp_vfs_mount(2, mnt_args, (mp_map_t *)&mp_const_empty_map);
    mp_obj_list_append(mp_sys_path, d);
    mp_printf(MP_PYTHON_PRINTER, "%s Mounted\n", self->drive);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(SDCard_mount_obj, SDCard_mount);

//__> EJECT ___________________________________________________________
STATIC mp_obj_t SDCard_eject(mp_obj_t self_in) {
    sdcard_SDCard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t d  = mp_obj_new_str(self->drive, strlen(self->drive));
    mp_vfs_umount(d);
    mp_obj_list_remove(mp_sys_path, d);
    mp_printf(MP_PYTHON_PRINTER, "%s Ejected\n", self->drive);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(SDCard_eject_obj, SDCard_eject);


STATIC mp_obj_t SDCard_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 8, true);
    sdcard_SDCard_obj_t *self = m_new_obj(sdcard_SDCard_obj_t);
    self->base.type = &sdcard_SDCard_type;
    
    enum {ARG_spi, ARG_sck, ARG_mosi, ARG_miso, ARG_cs, ARG_baudrate, ARG_mount, ARG_drive};
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_spi       , MP_ARG_REQUIRED | MP_ARG_INT , {.u_int     = 0                              }},
        { MP_QSTR_sck       , MP_ARG_REQUIRED | MP_ARG_INT , {.u_int     = 0                              }},
        { MP_QSTR_mosi      , MP_ARG_REQUIRED | MP_ARG_INT , {.u_int     = 0                              }},
        { MP_QSTR_miso      , MP_ARG_REQUIRED | MP_ARG_INT , {.u_int     = 0                              }},
        { MP_QSTR_cs        , MP_ARG_REQUIRED | MP_ARG_INT , {.u_int     = 0                              }},
        { MP_QSTR_baudrate  , MP_ARG_INT                   , {.u_int     = 0x500000                       }},
        { MP_QSTR_mount     , MP_ARG_BOOL                  , {.u_bool    = true                           }},
        { MP_QSTR_drive     , MP_ARG_OBJ                   , {.u_rom_obj = MP_ROM_QSTR(MP_QSTR__slash_sd) }},
    };
    
    mp_arg_val_t kw[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, kw);
    
    //setup spi pins
    gpio_set_function(kw[ARG_sck].u_int,  GPIO_FUNC_SPI);
    gpio_set_function(kw[ARG_mosi].u_int, GPIO_FUNC_SPI);
    gpio_set_function(kw[ARG_miso].u_int, GPIO_FUNC_SPI);
    
    //setup sdobject
    mp_obj_t sdo_args[3];
    sdo_args[0] = mp_obj_new_int(kw[ARG_spi].u_int);
    sdo_args[1] = mp_obj_new_int(kw[ARG_cs].u_int);
    sdo_args[2] = mp_obj_new_int(kw[ARG_baudrate].u_int);
    self->sdobject = MP_OBJ_TO_PTR(SDObject_make_new(NULL, 3, 0, sdo_args));
    
    //store drive letter
    mp_check_self(mp_obj_is_str_or_bytes(kw[ARG_drive].u_rom_obj));
    GET_STR_DATA_LEN(kw[ARG_drive].u_rom_obj, str, str_len);
    self->drive = (const char*)str;
    
    if (kw[ARG_mount].u_bool == true)
        SDCard_mount(MP_OBJ_FROM_PTR(self));
    
    return MP_OBJ_FROM_PTR(self);
}

STATIC const mp_rom_map_elem_t SDCard_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&SDCard_mount_obj) },
    { MP_ROM_QSTR(MP_QSTR_eject), MP_ROM_PTR(&SDCard_eject_obj) },
};

STATIC MP_DEFINE_CONST_DICT(SDCard_locals_dict, SDCard_locals_dict_table);

STATIC void SDCard_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    sdcard_SDCard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] == MP_OBJ_NULL) {
        if (attr == MP_QSTR_drive)
            dest[0] = mp_obj_new_str(self->drive, strlen(self->drive));
        else if (attr == MP_QSTR_type)
            dest[0] = mp_obj_new_str(self->sdobject->type, strlen(self->sdobject->type));
        else if (attr == MP_QSTR_sectors)
            dest[0] = mp_obj_new_int(self->sdobject->sectors);
        else if (attr == MP_QSTR_mount) {
            dest[0] = MP_OBJ_FROM_PTR(&SDCard_mount_obj);
            dest[1] = self;  
        }
        else if (attr == MP_QSTR_eject) {
            dest[0] = MP_OBJ_FROM_PTR(&SDCard_eject_obj);
            dest[1] = self;  
        }
    } 
}

const mp_obj_type_t sdcard_SDCard_type = {
    { &mp_type_type },
    .name        = MP_QSTR_SDCard,
    .print       = SDCard_print,
    .make_new    = SDCard_make_new,
    .locals_dict = (mp_obj_dict_t*)&SDCard_locals_dict,
    .attr        = SDCard_attr,
};

//__> MODULE __________________________________________________________________________
STATIC const mp_map_elem_t sdcard_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__) , MP_OBJ_NEW_QSTR(MP_QSTR_sdcard) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_SDObject) , (mp_obj_t)&sdcard_SDObject_type },
    { MP_OBJ_NEW_QSTR(MP_QSTR_SDCard)   , (mp_obj_t)&sdcard_SDCard_type   },
};

STATIC MP_DEFINE_CONST_DICT (mp_module_sdcard_globals, sdcard_globals_table);

const mp_obj_module_t sdcard_user_cmodule = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_sdcard_globals,
};

MP_REGISTER_MODULE(MP_QSTR_sdcard, sdcard_user_cmodule, MODULE_SDCARD_ENABLED);
