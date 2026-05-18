/**
 ****************************************************************************************************
 * @file        es8311.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-01
 * @brief       ES8311驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32S3 BOX 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "es8311.h"


const char* es8311_tag = "es8311";

i2c_master_dev_handle_t es8311_handle = NULL;

struct _coeff_div {
    uint32_t mclk;        /* 主时钟频率 */
    uint32_t rate;        /* 采样率 */
    uint8_t pre_div;      /* 预分频器（范围1-8） */
    uint8_t pre_multi;    /* 预倍频器（可选x1、x2、x4、x8） */
    uint8_t adc_div;      /* ADC时钟分频器 */
    uint8_t dac_div;      /* DAC时钟分频器 */
    uint8_t fs_mode;      /* 采样速率模式（0=单速模式，1=双速模式） */
    uint8_t lrck_h;       /* LRCK分频器高8位（主模式下用于生成LRCK） */
    uint8_t lrck_l;       /* LRCK分频器低8位（主模式下用于生成LRCK） */
    uint8_t bclk_div;     /* BCLK分频器（主模式下用于生成SCLK） */
    uint8_t adc_osr;      /* ADC过采样率系数 */
    uint8_t dac_osr;      /* DAC过采样率系数 */
};

static const struct _coeff_div coeff_div[] = {/* 定义时钟树:需根据芯片修改 */
    /*!<mclk     rate   pre_div  mult  adc_div dac_div fs_mode lrch  lrcl  bckdiv osr */

    /* 8k */
    {12288000, 8000, 0x06, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 8000, 0x03, 0x02, 0x03, 0x03, 0x00, 0x05, 0xff, 0x18, 0x10, 0x10},
    {16384000, 8000, 0x08, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 8000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 8000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000, 8000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 8000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000, 8000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 8000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1024000, 8000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 11.025k */
    {11289600, 11025, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 11025, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 11025, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 11025, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 12k */
    {12288000, 12000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 12000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 12000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 12000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 16k */
    {12288000, 16000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 16000, 0x03, 0x02, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x10},
    {16384000, 16000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 16000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 16000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000, 16000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 16000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000, 16000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 16000, 0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1024000, 16000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 22.05k */
    {11289600, 22050, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 22050, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 22050, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 22050, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 24k */
    {12288000, 24000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 24000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 24000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 24000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 24000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 32k */
    {12288000, 32000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 32000, 0x03, 0x04, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x10},
    {16384000, 32000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 32000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 32000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000, 32000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 32000, 0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000, 32000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 32000, 0x03, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
    {1024000, 32000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 44.1k */
    {11289600, 44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 44100, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 44100, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 44100, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 48k */
    {12288000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 48000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 48000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 48000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 48000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 64k */
    {12288000, 64000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 64000, 0x03, 0x04, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {16384000, 64000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 64000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 64000, 0x01, 0x04, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {4096000, 64000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 64000, 0x01, 0x08, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {2048000, 64000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 64000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0xbf, 0x03, 0x18, 0x18},
    {1024000, 64000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},

    /* 88.2k */
    {11289600, 88200, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 88200, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 88200, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 88200, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},

    /* 96k */
    {12288000, 96000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 96000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 96000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 96000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 96000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
};

/**
 * @brief       ES8311写寄存器
 * @param       reg_addr:寄存器地址
 * @param       data:写入的数据
 * @retval      无
 */
esp_err_t es8311_write_reg(uint8_t reg_addr, uint8_t data)
{
    esp_err_t ret;
    uint8_t *buf = malloc(2);

    if (buf == NULL)
    {
        ESP_LOGE(es8311_tag, "%s memory failed", __func__);
        return ESP_ERR_NO_MEM;      /* 分配内存失败 */
    }

    buf[0] = reg_addr;              
    buf[1] = data;                  /* 拷贝数据至存储区当中 */

    do 
    {
        i2c_master_bus_wait_all_done(bus_handle, 1000);
        ret = i2c_master_transmit(es8311_handle, buf, 2, 1000);   
    } while (ret != ESP_OK);

    free(buf);                      /* 发送完成释放内存 */

    return ret;
}

/**
 * @brief       ES8311读寄存器
 * @param       reg_add:寄存器地址
 * @retval      无
 */
esp_err_t es8311_read_reg(uint8_t reg_addr)
{
    uint8_t reg_data = 0;
    i2c_master_transmit_receive(es8311_handle, &reg_addr, 1, &reg_data, 1, -1);
    return reg_data;
}

/* 无需修改，时钟表查表函数，仅在c文件中声明，防止重复定义 */
static int get_coeff(uint32_t mclk, uint32_t rate)
{
    for (int i = 0; i < (sizeof(coeff_div) / sizeof(coeff_div[0])); i++) 
    {
        if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk) 
        {
            return i;
        }
    }

    return -1;
}

/**
 * @brief       ES8311三态模式设置数据和时钟
 * @param       tristate:如果tristate = 0，则在正常模式下禁用tristate；如果tristate = 1，则启用tristate
 * @retval      无
 */
void es8311_set_tristate(int tristate)
{
    uint8_t regv;
    ESP_LOGI(es8311_tag, "Enter into es8311_set_tristate(), tristate = %d\n", tristate);
    regv = es8311_read_reg(ES8311_CLK_MANAGER_REG07) & 0xcf;

    if (tristate) 
    {
        es8311_write_reg(ES8311_CLK_MANAGER_REG07, regv | 0x30);
    } 
    else 
    {
        es8311_write_reg(ES8311_CLK_MANAGER_REG07, regv);
    }
}

/**
 * @brief       是否将ES8311 DAC设置为静音
 * @param       mute:如果mute = 0， dac取消静音；如果mute = 1， dac静音
 * @retval      无
 */
static void es8311_mute(int mute)
{
    uint8_t regv;
    ESP_LOGI(es8311_tag, "Enter into es8311_mute(), mute = %d\n", mute);
    regv = es8311_read_reg(ES8311_DAC_REG31) & 0x9f;

    if (mute) 
    {
        es8311_write_reg(ES8311_DAC_REG31, regv | 0x60);
    }
    else
    {
        es8311_write_reg(ES8311_DAC_REG31, regv);
    }
}

/**
 * @brief       ES8311初始化
 * @param       sample_fre:采样率
 * @retval      0,初始化正常
 *              其他,错误代码
 */
esp_err_t es8311_init(int sample_fre)
{
    int coeff;
    esp_err_t ret = ESP_OK;
    uint8_t adc_iface, dac_iface, datmp, regv;
    
    if (sample_fre <= 8000) 
    {
        ESP_LOGE(es8311_tag, "es8311 init need  > 8000Hz frq ,such as 32000Hz, 44100kHz");
        return ESP_FAIL;
    }

    /* 未调用myiic_init初始化IIC */
    if (bus_handle == NULL)
    {
        ESP_ERROR_CHECK(myiic_init());
    }

    i2c_device_config_t es8311_i2c_dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,  /* 从机地址长度 */
        .scl_speed_hz    = IIC_SPEED_CLK,       /* 传输速率 */
        .device_address  = ES8311_ADDR,         /* 从机7位的地址 */
    };

    /* I2C总线上添加es8311设备 */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &es8311_i2c_dev_conf, &es8311_handle));
    ESP_ERROR_CHECK(i2c_master_bus_wait_all_done(bus_handle,1000));

    ret |= es8311_write_reg(ES8311_GP_REG45, 0x00);
    ret |= es8311_write_reg(ES8311_CLK_MANAGER_REG01, 0x30);
    ret |= es8311_write_reg(ES8311_CLK_MANAGER_REG02, 0x00);
    ret |= es8311_write_reg(ES8311_CLK_MANAGER_REG03, 0x10);
    ret |= es8311_write_reg(ES8311_ADC_REG16, 0x24);
    ret |= es8311_write_reg(ES8311_CLK_MANAGER_REG04, 0x10);
    ret |= es8311_write_reg(ES8311_CLK_MANAGER_REG05, 0x00);
    ret |= es8311_write_reg(ES8311_SYSTEM_REG0B, 0x00);
    ret |= es8311_write_reg(ES8311_SYSTEM_REG0C, 0x00);
    ret |= es8311_write_reg(ES8311_SYSTEM_REG10, 0x1F);
    ret |= es8311_write_reg(ES8311_SYSTEM_REG11, 0x7F);
    ret |= es8311_write_reg(ES8311_RESET_REG00, 0x80);
    vTaskDelay(pdMS_TO_TICKS(80));

    /* 设置主从模式 */
    regv  = es8311_read_reg(ES8311_RESET_REG00);
    regv &= 0xBF;
    ret  |= es8311_write_reg(ES8311_RESET_REG00, regv);
    ret  |= es8311_write_reg(ES8311_SYSTEM_REG0D, 0x01);
    ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG01, 0x3F);
    ESP_LOGI(es8311_tag, "ES8311 in Slave mode\n");

    /* 选择内部MCLK的时钟源 */
    regv  = es8311_read_reg(ES8311_CLK_MANAGER_REG01);
    regv |= 0x80;
    ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG01, regv);

    int mclk_fre = 0;
    mclk_fre = sample_fre * MCLK_DIV_FRE;
    coeff = get_coeff(mclk_fre, sample_fre);

    if (coeff < 0) 
    {
        ESP_LOGE(es8311_tag, "Unable to configure sample rate %dHz with %dHz MCLK\n", sample_fre, mclk_fre);
        return ESP_FAIL;
    }

    /* 设置时钟参数 */
    if (coeff >= 0) 
    {
        regv = es8311_read_reg(ES8311_CLK_MANAGER_REG02) & 0x07;
        regv |= (coeff_div[coeff].pre_div - 1) << 5;
        datmp = 0;

        switch (coeff_div[coeff].pre_multi) 
        {
            case 1:
            {
                datmp = 0;
                break;
            }

            case 2:
            {
                datmp = 1;
                break;
            }

            case 4:
            {
                datmp = 2;
                break;
            }

            case 8:
            {
                datmp = 3;
                break;
            }

            default:
            {
                break;
            }
        }

        regv |= (datmp) << 3;
        ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG02, regv);

        regv  = es8311_read_reg(ES8311_CLK_MANAGER_REG05) & 0x00;
        regv |= (coeff_div[coeff].adc_div - 1) << 4;
        regv |= (coeff_div[coeff].dac_div - 1) << 0;
        ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG05, regv);

        regv  = es8311_read_reg(ES8311_CLK_MANAGER_REG03) & 0x80;
        regv |= coeff_div[coeff].fs_mode << 6;
        regv |= coeff_div[coeff].adc_osr << 0;
        ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG03, regv);

        regv  = es8311_read_reg(ES8311_CLK_MANAGER_REG04) & 0x80;
        regv |= coeff_div[coeff].dac_osr << 0;
        ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG04, regv);

        regv  = es8311_read_reg(ES8311_CLK_MANAGER_REG07) & 0xC0;
        regv |= coeff_div[coeff].lrck_h << 0;
        ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG07, regv);

        regv  = es8311_read_reg(ES8311_CLK_MANAGER_REG08) & 0x00;
        regv |= coeff_div[coeff].lrck_l << 0;
        ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG08, regv);

        regv  = es8311_read_reg(ES8311_CLK_MANAGER_REG06) & 0xE0;
        vTaskDelay(pdMS_TO_TICKS(80));

        if (coeff_div[coeff].bclk_div < 19) 
        {
            regv |= (coeff_div[coeff].bclk_div - 1) << 0;
        } 
        else 
        {
            regv |= (coeff_div[coeff].bclk_div) << 0;
        }

        ret |= es8311_write_reg(ES8311_CLK_MANAGER_REG06, regv);
    }

    /* DAC/ADC接口，DAC/ADC分辨率 */
    dac_iface = es8311_read_reg(ES8311_SDPIN_REG09) & 0xC0;
    adc_iface = es8311_read_reg(ES8311_SDPOUT_REG0A) & 0xC0;

    /* bit size */
    dac_iface |= 0x0c;
    adc_iface |= 0x0c;

    /* 设置接口格式 */
    dac_iface &= 0xFC;
    adc_iface &= 0xFC;

    /* 设置iface */
    ret |= es8311_write_reg(ES8311_SDPIN_REG09, dac_iface);
    ret |= es8311_write_reg(ES8311_SDPOUT_REG0A, adc_iface);
    ESP_LOGI(es8311_tag, "ES8311 in I2S Format\n");

    /* MCLK时钟翻转 */
    if (INVERT_MCLK == 1) 
    {
        regv  = es8311_read_reg(ES8311_CLK_MANAGER_REG01);
        regv |= 0x40;
        ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG01, regv);
    } 
    else 
    {
        regv  = es8311_read_reg(ES8311_CLK_MANAGER_REG01);
        regv &= ~(0x40);
        ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG01, regv);
    }

    /* SCLK时钟翻转 */
    if (INVERT_SCLK == 1) 
    {
        regv  = es8311_read_reg(ES8311_CLK_MANAGER_REG06);
        regv |= 0x20;
        ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG06, regv);
    } 
    else 
    {
        regv  = es8311_read_reg(ES8311_CLK_MANAGER_REG06);
        regv &= ~(0x20);
        ret  |= es8311_write_reg(ES8311_CLK_MANAGER_REG06, regv);
    }

    ret |= es8311_write_reg(ES8311_SYSTEM_REG14, 0x1A);

    /* 禁用/使能数字麦克风 */
    if (IS_DMIC == 1)
    {
        regv  = es8311_read_reg(ES8311_SYSTEM_REG14);
        regv |= 0x40;
        ret  |= es8311_write_reg(ES8311_SYSTEM_REG14, regv);
    } 
    else
    {
        regv  = es8311_read_reg(ES8311_SYSTEM_REG14);
        regv &= ~(0x40);
        ret  |= es8311_write_reg(ES8311_SYSTEM_REG14, regv);
    }

    ret |= es8311_write_reg(ES8311_SYSTEM_REG12, 0x00);
    ret |= es8311_write_reg(ES8311_SYSTEM_REG13, 0x10);
    ret |= es8311_write_reg(ES8311_SYSTEM_REG0E, 0x02);
    ret |= es8311_write_reg(ES8311_ADC_REG15, 0x40);
    ret |= es8311_write_reg(ES8311_ADC_REG1B, 0x0A);
    ret |= es8311_write_reg(ES8311_ADC_REG1C, 0x6A);
    ret |= es8311_write_reg(ES8311_DAC_REG37, 0x48);
    ret |= es8311_write_reg(ES8311_GPIO_REG44, 0x08);
    ret |= es8311_write_reg(ES8311_ADC_REG17, 0xBF);
    ret |= es8311_write_reg(ES8311_DAC_REG32, 0xBF);

    if (ret != ESP_OK)
    {
        ESP_LOGI(es8311_tag, "ES8311 fail");
        return 1;
    }
    else
    {
        ESP_LOGI(es8311_tag, "ES8311 success");
        vTaskDelay(pdMS_TO_TICKS(100));
        return 0;
    }

    es8311_set_voice_volume(0); /* 设置喇叭音量 */
    es8311_set_voice_mute(1);   /* DAC静音 */

    return ESP_OK;
}

/**
 * @brief       ES8311设置音量大小
 * @param       volume:用于设置音量
 * @retval      0,初始化正常
 *              其他,错误代码
 */
esp_err_t es8311_set_voice_volume(int volume)
{
    int res = 0;

    if (volume < 0) 
    {
        volume = 0;
    } 

    else if (volume > 90) 
    {
        volume = 70;
    }

    int vol = (volume) * 2550 / 1000 + 0.5;
    // ESP_LOGI(es8311_tag, "SET: volume:%d\n", vol);
    es8311_write_reg(ES8311_DAC_REG32, vol);

    return res;
}

/**
 * @brief       ES8311设置DAC静音
 * @param       enable:enable = 0，dac取消静音；如果enable = 1，dac静音
 * @retval      0,初始化正常
 *              其他,错误代码
 */
int es8311_set_voice_mute(int enable)
{
    int res = 0;

    ESP_LOGI(es8311_tag, "Es8311SetVoiceMute mute:%d\n", enable);
    es8311_mute(enable);

    return res;
}

/**
 * @brief       ES8311设置MIC增益
 * @param       gain_db:该参数的类型为 es8311_mic_gain_t，通常代表要设置的麦克风增益的分贝数。调用该函数时，需要传入合适的增益值，以便将其写入到 ES8311 芯片的指定寄存器中
 * @retval      0,初始化正常
 *              其他,错误代码
 */
int es8311_set_mic_gain(es8311_mic_gain_t gain_db)
{
    int res = 0;

    res = es8311_write_reg(ES8311_ADC_REG16, gain_db); /* 设置MIC增益大小 */

    return res;
}
