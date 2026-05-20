/**
 ****************************************************************************************************
 * @file        wavplay.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2024-06-25
 * @brief       wav解码 代码
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
#include "wavplay.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/******************************************************************************************************/
/*FreeRTOS配置*/

/* MUSIC 任务 配置
 * 包括: 任务句柄 任务优先级 堆栈大小 创建任务
 */
#define MUSIC_PRIO      4                   /* 任务优先级 */
#define MUSIC_STK_SIZE  15*1024              /* 任务堆栈大小 */
TaskHandle_t            MUSICTask_Handler;  /* 任务句柄 */
void music(void *pvParameters);             /* 任务函数 */

static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;

extern uint8_t uart_config1,pause_config1,music_key1; // 串口控制标志位

const char* wav = "wav";
/******************************************************************************************************/

__wavctrl wavctrl;                          /* WAV音频文件解码参数结构体 */
UINT bytes_write = 0;                       /* 写一次I2S大小 */
volatile long long int i2s_table_size = 0;  /* 积累每次发送音频数据总大小 */
esp_err_t i2s_play_end = ESP_FAIL;          /* 播放结束标志位 */
esp_err_t i2s_play_next_prev = ESP_FAIL;    /* 下一首或者上一首标志位 */
FSIZE_t file_read_pos = 0;                  /* 记录当前WAV读取位置 */

/**
* @brief       WAV解析初始化
* @param       fname : 文件路径+文件名
* @param       wavx  : 信息存放结构体指针
* @retval      0,打开文件成功
*              1,打开文件失败
*              2,非WAV文件
*              3,DATA区域未找到
*/
uint8_t wav_decode_init(uint8_t *fname, __wavctrl *wavx)
{
   FIL *ftemp;
   uint8_t *buf; 
   uint32_t br = 0;
   uint8_t res = 0;

   ChunkRIFF *riff;
   ChunkFMT *fmt;
   ChunkFACT *fact;
   ChunkDATA *data;
   
   ftemp = (FIL*)malloc(sizeof(FIL));
   buf = malloc(512);
   
   if (ftemp && buf)
   {
       res = f_open(ftemp, (TCHAR*)fname, FA_READ);            /* 打开文件 */
       
       if (res == FR_OK)
       {
           f_read(ftemp, buf, 512, (UINT *)&br);               /* 读取512字节在数据 */
           riff = (ChunkRIFF *)buf;
           
           if (riff->Format == 0x45564157)                     /* 是WAV文件 */
           {
               fmt = (ChunkFMT *)(buf + 12);
               fact = (ChunkFACT *)(buf + 12 + 8 + fmt->ChunkSize);
               
               if (fact->ChunkID == 0x74636166 || fact->ChunkID == 0x5453494C)
               {
                   wavx->datastart = 12 + 8 + fmt->ChunkSize + 8 + fact->ChunkSize;
               }
               else
               {
                   wavx->datastart = 12 + 8 + fmt->ChunkSize;
               }
               
               data = (ChunkDATA *)(buf + wavx->datastart);
               
               if (data->ChunkID == 0x61746164)                /* 解析成功 */
               {
                   wavx->audioformat = fmt->AudioFormat;       /* 音频格式 */
                   wavx->nchannels = fmt->NumOfChannels;       /* 通道数 */
                   wavx->samplerate = fmt->SampleRate;         /* 采样率 */
                   wavx->bitrate = fmt->ByteRate * 8;
                   wavx->blockalign = fmt->BlockAlign;
                   wavx->bps = fmt->BitsPerSample;
                   
                   wavx->datasize = data->ChunkSize;
                   wavx->datastart = wavx->datastart + 8;
                    
                   ESP_LOGI(wav,"wavx->audioformat:%d\r\n", wavx->audioformat);
                   ESP_LOGI(wav,"wavx->nchannels:%d\r\n", wavx->nchannels);
                   ESP_LOGI(wav,"wavx->samplerate:%ld\r\n", wavx->samplerate);
                   ESP_LOGI(wav,"wavx->bitrate:%ld\r\n", wavx->bitrate);
                   ESP_LOGI(wav,"wavx->blockalign:%d\r\n", wavx->blockalign);
                   ESP_LOGI(wav,"wavx->bps:%d\r\n", wavx->bps);
                   ESP_LOGI(wav,"wavx->datasize:%ld\r\n", wavx->datasize);
                   ESP_LOGI(wav,"wavx->datastart:%ld\r\n", wavx->datastart); 
               }
               else
               {
                   res = 3;
               }
           }
           else
           {
               res = 2;
           }
       }
       else
       {
           res = 1;
       }
   }
   
   f_close(ftemp);
   free(ftemp);
   free(buf); 
   
   return 0;
}

/**
* @brief       获取当前播放时间
* @param       fx    : 文件指针
* @param       wavx  : wavx播放控制器
* @retval      无
*/
void wav_get_curtime(FIL *fx, __wavctrl *wavx)
{
   long long fpos = 0;

   wavx->totsec = wavx->datasize / (wavx->bitrate / 8);    /* 歌曲总长度(单位:秒) */
   fpos = fx->fptr-wavx->datastart;                        /* 得到当前文件播放到的地方 */
   wavx->cursec = fpos * wavx->totsec / wavx->datasize;    /* 当前播放到第多少秒了? */
}

/**
* @brief       music任务
* @param       pvParameters : 传入参数(未用到)
* @retval      无
*/
void music(void *pvParameters)
{
   pvParameters = pvParameters;

   es8311_set_voice_volume(65);                     /* 设置喇叭音量，建议不超过65 */
   es8311_set_voice_mute(0);                        /* 打开DAC */
   xl9555_pin_write(SPK_CTRL_IO, 1);                /* 打开喇叭 */
   vTaskDelay(pdMS_TO_TICKS(20));


//    i2s_tx_write(g_audiodev.tbuf, WAV_TX_BUFSIZE);   /* 先发送一段无声音的数据 */

//    if ((g_audiodev.status & 0x0F) == 0x03)
//    {
//        i2s_tx_write(g_audiodev.tbuf, WAV_TX_BUFSIZE);
//    }

   while(1)
   {
       if ((g_audiodev.status & 0x0F) == 0x03)      /* 打开了音频 */
       {
           for(uint16_t readTimes = 0; readTimes < (wavctrl.datasize / WAV_TX_BUFSIZE); readTimes++)
           {
               if ((g_audiodev.status & 0x0F) == 0x00)             /* 暂停播放 */
               {
                   file_read_pos = f_tell(g_audiodev.file);        /* 记录暂停位置 */

                   while(1)
                   {
                       if ((g_audiodev.status & 0x0F) == 0x03)     /* 重新打开了 */
                       {
                           break;
                       }

                       vTaskDelay(pdMS_TO_TICKS(5));               /* 死等 */
                   }

                   f_lseek(g_audiodev.file, file_read_pos);        /* 跳过到之前停止的位置 */
                   es8311_set_voice_mute(0);              /* 恢复播放时取消静音 */
               }

               /* 判断是否播放完成 */
               if (i2s_table_size >= wavctrl.datasize || i2s_play_next_prev == ESP_OK)
               {
				    if((g_audiodev.status & 0x0F) == 0x03)
    				{
        				audio_stop();                   /* 先停止播放 */
    				}
                   //i2s_deinit();                   /* 卸载I2S */
                   i2s_table_size = 0;             /* 总大小清零 */
                   i2s_play_end = ESP_OK;          /* 已播放完成标志位 */
                   vTaskDelete(NULL);              /* 删除当前任务 */
                   vTaskDelay(pdMS_TO_TICKS(5));   /* 适当延时（为了删除这个任务） */
                   break;                          /* 防止延时5ms未能删除音频任务 */
               }

               // 🔥 核心防护：仅播放时写入数据（保留原逻辑，只加这1行判断）
               if ((g_audiodev.status & 0x0F) == 0x03)
               {
               		f_read(g_audiodev.file,g_audiodev.tbuf, WAV_TX_BUFSIZE, (UINT*)&bytes_write);
               		if ((g_audiodev.status & 0x0F) == 0x03)
               		{
               			i2s_table_size = i2s_table_size + i2s_tx_write(g_audiodev.tbuf, WAV_TX_BUFSIZE);
               		}
				vTaskDelay(pdMS_TO_TICKS(3)); /* 适当延时，降低CPU占用（尤其是高采样率大文件时） */
			}
				vTaskDelay(pdMS_TO_TICKS(1)); 
           }
       }

       vTaskDelay(pdMS_TO_TICKS(1));
   }

   vTaskDelete(NULL);
}

/**
* @brief       播放某个wav文件
* @param       fname : 文件路径+文件名
* @retval      KEY0_PRES : 下一首
*              KEY1_PRES : 上一首
*              KEY2_PRES : 停止/启动
*              其他,非WAV文件
*/

// 1. 【顶部添加】静态缓存变量（放在 wavplay.c 最上方，全局变量下面）
static uint32_t s_current_sample_rate = 0;        // 缓存当前采样率
static i2s_bits_per_sample_t s_current_bits = I2S_BITS_PER_SAMPLE_16BIT; // 缓存当前位宽

uint8_t wav_play_song(uint8_t *fname)
{
   uint8_t key = 0;
   uint8_t key1 = 0;
   uint8_t res = 0;
   uint8_t t = 0;
   uint8_t uart_key = 0;  // 记录串口切歌方向
   
   i2s_play_end = ESP_FAIL;
   i2s_play_next_prev = ESP_FAIL;
   g_audiodev.file = (FIL*)heap_caps_malloc(sizeof(FIL),MALLOC_CAP_DMA);
   g_audiodev.tbuf = heap_caps_malloc(WAV_TX_BUFSIZE, MALLOC_CAP_DMA);       /* 音频数据 */

   //myi2s_init();                                   /* I2S初始化 */
   vTaskDelay(pdMS_TO_TICKS(50));                  /* 适当延时 */

   if (g_audiodev.file || g_audiodev.tbuf)
   {
       memset(g_audiodev.file,0,sizeof(FIL));      /* 文件指针清零 */
       memset(g_audiodev.tbuf,0,WAV_TX_BUFSIZE);   /* buf清零 */
       memset(&wavctrl,0,sizeof(__wavctrl));       /* 对WAV结构体相关参数清零 */
       res = wav_decode_init(fname, &wavctrl);     /* 对wav音频文件解码 */

       if (res == 0)                               /* 解码成功 */
       {
		// 2. 【强制初始化】杜绝未初始化报错
        i2s_bits_per_sample_t set_bits = I2S_BITS_PER_SAMPLE_16BIT;

        //    if (wavctrl.bps == 16)                  /* 根据解码文件重新配置采样率和位宽 */
        //    {
        //        i2s_set_samplerate_bits_sample(wavctrl.samplerate,I2S_BITS_PER_SAMPLE_16BIT);
        //    }
        //    else if (wavctrl.bps == 24)
        //    {
        //        i2s_set_samplerate_bits_sample(wavctrl.samplerate,I2S_BITS_PER_SAMPLE_24BIT);
        //    }
	if (wavctrl.bps == 24)
	{
    	set_bits = I2S_BITS_PER_SAMPLE_24BIT;
	}


    // 仅配置变化时更新I2S
    if ((wavctrl.samplerate != s_current_sample_rate) || (set_bits != s_current_bits))
    {
        s_current_sample_rate = wavctrl.samplerate;
        s_current_bits = set_bits;
        i2s_set_samplerate_bits_sample(wavctrl.samplerate, set_bits);
    }

           res = f_open(g_audiodev.file, (TCHAR*)fname, FA_READ);      /* 打开WAV音频文件 */

           if (res == FR_OK)
           {
				f_lseek(g_audiodev.file, wavctrl.datastart);

    			if ((g_audiodev.status & 0x0F) == 0x00)
    			{
        			audio_start();  // 开启I2S
    			}/* 打开成功后，才创建音频任务 */

				// 🔥 新增：切歌前强制清理旧的音乐任务（防止多任务冲突）
				if (MUSICTask_Handler != NULL)
				{
    				vTaskDelete(MUSICTask_Handler);   // 删除旧任务
   					 MUSICTask_Handler = NULL;         // 句柄清零
				}
				vTaskDelay(pdMS_TO_TICKS(10));

               if (res == FR_OK)
               {
                   taskENTER_CRITICAL(&my_spinlock);
                   xTaskCreatePinnedToCore(
    					music,                  // 任务函数
   						"music",                // 任务名称
    					MUSIC_STK_SIZE,          // 堆栈大小（用宏定义，更稳定）
    					NULL,                   // 任务参数
    					MUSIC_PRIO,              // 任务优先级（保持原4）
    					&MUSICTask_Handler,      // 任务句柄
    					1                       // 🔥 关键：绑定到 CPU1 运行
				);
                   taskEXIT_CRITICAL(&my_spinlock);
               }

               while (res == 0)
               { 
                   while (1)
                   {
                       if (i2s_play_end == ESP_OK)
                       {
                           res = (uart_key != 0) ? uart_key : KEY0_PRES;
                           break;
                       }


                       key = xl9555_key_scan(0);
                       key1 = key_scan(0);
                       
						if(uart_config1 == 1)//串口控制标志位
						{
							ESP_LOGI("UART", "检测到串口指令：pause=%d, music_key=%d", pause_config1, music_key1); // 新增日志
							vTaskSuspendAll(); // 挂起所有任务，安全修改共享变量
							if(pause_config1 == 1 || pause_config1 == 2) //暂停/播放
							{
								key1 = BOOT_PRES; // 暂停/播放按键
								// pause_config1 = 0; // 触发后立即重置
							}

							 if(music_key1 == KEY0_PRES) //下一首
							{
								uart_key = KEY0_PRES; // 记录下一首方向
								i2s_play_next_prev = ESP_OK;
								music_key1 = 0; // 触发后立即重置
							}

							else if(music_key1 == KEY1_PRES) //上一首
							{
								uart_key = KEY1_PRES; // 记录上一首方向
								i2s_play_next_prev = ESP_OK;
								music_key1 = 0; // 触发后立即重置
							}

							uart_config1 = 0;
							xTaskResumeAll(); // 恢复任务
						}

					//    if(key1 == KEY1_PRES || key1 == KEY0_PRES) break;

                       if (key1 == BOOT_PRES)                                   /* 暂停 */
                       {
                           if ((g_audiodev.status & 0x0F) == 0x03 && pause_config1 == 1)
                           {
                               audio_stop();
							   pause_config1 = 0; // 触发后立即重置
                           }
                           else if ((g_audiodev.status & 0x0F) == 0x00 && pause_config1 == 2)
                           {
                               audio_start();
							   pause_config1 = 0; // 触发后立即重置
                           }
                       }
                       		

                       if (key == KEY1_PRES || key == KEY0_PRES)               /* 下一曲/上一曲 */
                       {
                           i2s_play_next_prev = ESP_OK;
                           break;
                       }
                       
                       if ((g_audiodev.status & 0x0F) == 0x03)                 /* 暂停不刷新时间 */
                       {
                           wav_get_curtime(g_audiodev.file, &wavctrl);         /* 得到总时间和当前播放的时间 */
                           audio_msg_show(wavctrl.totsec, wavctrl.cursec, wavctrl.bitrate);
                       }
                       
                       t++;
                       if (t == 20)
                       {
                           t = 0 ;
                           LED_TOGGLE();
                       }
                       
                       if ((g_audiodev.status & 0x01) == 0)
                       {
                            vTaskDelay(pdMS_TO_TICKS(10));
                       }
                       else
                       {
                           break;
                       }
                   }

                   if (key == KEY1_PRES || key == KEY0_PRES)                   /* 退出切换歌曲 */
                   {
                       res = key;
                       break;
                   }
               }
           }
           else
           {
               res = 0xFF;
           }
       }
       else
       {
           res = 0xFF;
       }
   }
   else
   {
       res = 0xFF;
   }


   heap_caps_free(g_audiodev.file);
   heap_caps_free(g_audiodev.tbuf);
   g_audiodev.tbuf = NULL;
   g_audiodev.file = NULL;
   MUSICTask_Handler = NULL;
   return res;
}