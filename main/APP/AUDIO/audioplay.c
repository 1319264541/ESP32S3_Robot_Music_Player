/**
 ****************************************************************************************************
 * @file        audioplay.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2024-06-25
 * @brief       音乐播放器 应用代码
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

#include "audioplay.h"


const char* audioplay = "audioplay";

extern uint8_t uart_config1,pause_config1,music_key1; // 串口控制标志位

__audiodev g_audiodev;          /* 音乐播放控制器 */

/**
 * @brief       开始音频播放
 * @param       无
 * @retval      无
 */
void audio_start(void)
{
    g_audiodev.status = 3 << 0; /* 开始播放+非暂停 */
    i2s_trx_start();
}

/**
 * @brief       停止音频播放
 * @param       无
 * @retval      无
 */
void audio_stop(void)
{
    g_audiodev.status = 0;
    i2s_trx_stop();
}

/**
 * @brief       得到path路径下，目标文件的总数
 * @param       path : 文件路径
 * @retval      有效文件总数
 */
uint16_t audio_get_tnum(uint8_t *path)
{
    uint8_t res;
    uint16_t rval = 0;
    FF_DIR tdir;                                                /* 临时目录 */
    FILINFO *tfileinfo;                                         /* 临时文件信息 */
    
    tfileinfo = (FILINFO*)malloc(sizeof(FILINFO));              /* 申请内存 */
    
    res = f_opendir(&tdir, (const TCHAR*)path);                 /* 打开目录 */
    
    if ((res == FR_OK) && tfileinfo)
    {
        while (1)                                               /* 查询总的有效文件数 */
        {
            res = f_readdir(&tdir, tfileinfo);                  /* 读取目录下的一个文件 */
            
            if ((res != FR_OK) || (tfileinfo->fname[0] == 0))
            {
                break;                                          /* 错误了/到末尾了,退出 */
            }

            res = exfuns_file_type(tfileinfo->fname);
            
            if ((res & 0xF0) == 0x40)                           /* 取高四位,看看是不是音乐文件 */
            {
                rval++;                                         /* 有效文件数增加1 */
            }
        }
    }
    
    free(tfileinfo);                                            /* 释放内存 */
    
    return rval;
}

/**
 * @brief       显示曲目索引
 * @param       index : 当前索引
 * @param       total : 总文件数
 * @retval      无
 */
void audio_index_show(uint16_t index, uint16_t total)
{
    /* 显示当前曲目的索引,及总曲目数 */
    lcd_show_num(30 + 0, 230, index, 3, 16, RED);   /* 索引 */
    lcd_show_char(30 + 24, 230, '/', 16, 0, RED);
    lcd_show_num(30 + 32, 230, total, 3, 16, RED);  /* 总曲目 */
}

/**
 * @brief       显示播放时间,比特率 信息
 * @param       totsec : 音频文件总时间长度
 * @param       cursec : 当前播放时间
 * @param       bitrate: 比特率(位速)
 * @retval      无
 */
void audio_msg_show(uint32_t totsec, uint32_t cursec, uint32_t bitrate)
{
    static uint16_t playtime = 0xFFFF;                                  /* 放时间标记 */
    
    if (playtime != cursec)                                             /* 需要更新显示时间 */
    {
        playtime = cursec;
        
        /* 显示播放时间 */
        lcd_show_xnum(30, 210, playtime / 60, 2, 16, 0X80, RED);        /* 分钟 */
        lcd_show_char(30 + 16, 210, ':', 16, 0, RED);
        lcd_show_xnum(30 + 24, 210, playtime % 60, 2, 16, 0X80, RED);   /* 秒钟 */
        lcd_show_char(30 + 40, 210, '/', 16, 0, RED);
        
        /* 显示总时间 */
        lcd_show_xnum(30 + 48, 210, totsec / 60, 2, 16, 0X80, RED);     /* 分钟 */
        lcd_show_char(30 + 64, 210, ':', 16, 0, RED);
        lcd_show_xnum(30 + 72, 210, totsec % 60, 2, 16, 0X80, RED);     /* 秒钟 */
        
        /* 显示位率 */
        lcd_show_num(30 + 110, 210, bitrate / 1000, 4, 16, RED);/* 显示位率 */
        lcd_show_string(30 + 110 + 32 , 210, 200, 16, 16, "Kbps", RED);
    }
}

/**
 * @brief       转换
 * @param       fs:文件系统对象
 * @param       clst:转换
 * @retval      =0:扇区号，0:失败
 */
static LBA_t atk_clst2sect(FATFS *fs, DWORD clst)
{
    clst -= 2;  /* Cluster number is origin from 2 */

    if (clst >= fs->n_fatent - 2)
    {
        return 0;   /* Is it invalid cluster number? */
    }

    return fs->database + (LBA_t)fs->csize * clst;  /* Start sector number of the cluster */
}

/**
 * @brief       偏移
 * @param       dp:指向目录对象
 * @param       Offset:目录表的偏移量
 * @retval      FR_OK(0):成功，!=0:错误
 */
FRESULT atk_dir_sdi(FF_DIR *dp, DWORD ofs)
{
    DWORD clst;
    FATFS *fs = dp->obj.fs;

    if (ofs >= (DWORD)((FF_FS_EXFAT && fs->fs_type == FS_EXFAT) ? 0x10000000 : 0x200000) || ofs % 32)
    {
        /* Check range of offset and alignment */
        return FR_INT_ERR;
    }

    dp->dptr = ofs;         /* Set current offset */
    clst = dp->obj.sclust;  /* Table start cluster (0:root) */

    if (clst == 0 && fs->fs_type >= FS_FAT32)
    {	/* Replace cluster# 0 with root cluster# */
        clst = (DWORD)fs->dirbase;

        if (FF_FS_EXFAT)
        {
            dp->obj.stat = 0;
        }
        /* exFAT: Root dir has an FAT chain */
    }

    if (clst == 0)
    {	/* Static table (root-directory on the FAT volume) */
        if (ofs / 32 >= fs->n_rootdir)
        {
            return FR_INT_ERR;  /* Is index out of range? */
        }

        dp->sect = fs->dirbase;

    }
    else
    {   /* Dynamic table (sub-directory or root-directory on the FAT32/exFAT volume) */
        dp->sect = atk_clst2sect(fs, clst);
    }

    dp->clust = clst;   /* Current cluster# */

    if (dp->sect == 0)
    {
        return FR_INT_ERR;
    }

    dp->sect += ofs / fs->ssize;             /* Sector# of the directory entry */
    dp->dir = fs->win + (ofs % fs->ssize);   /* Pointer to the entry in the win[] */

    return FR_OK;
}

/**
 * @brief       播放音乐
 * @param       无
 * @retval      无
 */
void audio_play(void)
{
    uint8_t res;
    FF_DIR wavdir;                                              /* 目录 */
    FILINFO *wavfileinfo;                                       /* 文件信息 */
    uint8_t *pname;                                             /* 带路径的文件名 */
    uint16_t totwavnum;                                         /* 音乐文件总数 */
    uint16_t curindex;                                          /* 当前索引 */
    uint8_t key;                                                /* 键值 */
    uint32_t temp;
    uint32_t *wavoffsettbl;                                     /* 音乐offset索引表 */

    while (f_opendir(&wavdir, "0:/MUSIC"))                      /* 打开音乐文件夹 */
    {
        text_show_string(30, 190, 240, 16, "MUSIC文件夹错误!", 16, 0, BLUE);
        vTaskDelay(200);
        lcd_fill(30, 190, 240, 206, WHITE);                     /* 清除显示 */
        vTaskDelay(200);
    }

    totwavnum = audio_get_tnum((uint8_t *)"0:/MUSIC");          /* 得到总有效文件数 */
    
    while (totwavnum == 0)                                      /* 音乐文件总数为0 */
    {
        text_show_string(30, 190, 240, 16, "没有音乐文件!", 16, 0, BLUE);
        vTaskDelay(200);
        lcd_fill(30, 190, 240, 146, WHITE);                     /* 清除显示 */
        vTaskDelay(200);
    }
    
    wavfileinfo = (FILINFO*)malloc(sizeof(FILINFO));            /* 申请内存 */
    pname = malloc(255 * 2 + 1);                                /* 为带路径的文件名分配内存 */
    wavoffsettbl = malloc(4 * totwavnum);                       /* 申请4*totwavnum个字节的内存,用于存放音乐文件off block索引 */
    
    while (!wavfileinfo || !pname || !wavoffsettbl)             /* 内存分配出错 */
    {
        text_show_string(30, 190, 240, 16, "内存分配失败!", 16, 0, BLUE);
        vTaskDelay(200);
        lcd_fill(30, 190, 240, 146, WHITE);               /* 清除显示 */
        vTaskDelay(200);
    }
    
    /* 记录索引 */
    res = f_opendir(&wavdir, "0:/MUSIC");                       /* 打开目录 */
    
    if (res == FR_OK)
    {
        curindex = 0;                                           /* 当前索引为0 */
        
        while (1)                                               /* 全部查询一遍 */
        {
            temp = wavdir.dptr;                                 /* 记录当前index */

            res = f_readdir(&wavdir, wavfileinfo);              /* 读取目录下的一个文件 */
            
            if ((res != FR_OK) || (wavfileinfo->fname[0] == 0))
            {
                break;                                          /* 错误了/到末尾了,退出 */
            }

            res = exfuns_file_type(wavfileinfo->fname);
            
            if ((res & 0xF0) == 0x40)                           /* 取高四位,看看是不是音乐文件 */
            {
                wavoffsettbl[curindex] = temp;                   /* 记录索引 */
                curindex++;
            }
        }
    }
    
    curindex = 0;                                               /* 从0开始显示 */
    res = f_opendir(&wavdir, (const TCHAR*)"0:/MUSIC");         /* 打开目录 */
    
    while (res == FR_OK)                                        /* 打开成功 */
    {
        atk_dir_sdi(&wavdir, wavoffsettbl[curindex]);               /* 改变当前目录索引 */
        res = f_readdir(&wavdir, wavfileinfo);                  /* 读取目录下的一个文件 */
        
        if ((res != FR_OK) || (wavfileinfo->fname[0] == 0))
        {
            break;                                              /* 错误了/到末尾了,退出 */
        }
        
        strcpy((char *)pname, "0:/MUSIC/");                     /* 复制路径(目录) */
        strcat((char *)pname, (const char *)wavfileinfo->fname);/* 将文件名接在后面 */
        lcd_fill(30, 190, lcd_dev.width, lcd_dev.height, WHITE); /* 清除之前的显示 */
        audio_index_show(curindex + 1, totwavnum);
        text_show_string(30, 190, lcd_dev.width - 60, 16, (char *)wavfileinfo->fname, 16, 0, BLUE);   /* 显示歌曲名字 */
        
		key = audio_play_song(pname);                           /* 播放这个音频文件 */

		if(uart_config1 == 1) // 如果串口控制标志位被设置，优先处理串口指令
		{
			uart_config1 = 0; // 重置串口控制标志位
			    // ? 只修改这行：如果是切歌指令（上/下首），才赋值！不破坏原有逻辑
    		if(music_key1 == KEY0_PRES || music_key1 == KEY1_PRES)
    		{
        		key = music_key1;
    		}
			// key = music_key1; // 获取串口指令（播放/暂停/下一曲等）
			music_key1 = 0; // 重置音乐控制键值
		}

        if (key == KEY1_PRES)                                   /* 上一曲 */
        {
            if (curindex)
            {
                curindex--;
            }
            else
            {
                curindex = totwavnum - 1;
            }
        }
        else if (key == KEY0_PRES)                              /* 下一曲 */
        {
            curindex++;

            if (curindex >= totwavnum)
            {
                curindex = 0;                                   /* 到末尾的时候,自动从头开始 */
            }
        }
        else
        {
            break;                                              /* 产生了错误 */
        }
    }

    free(wavfileinfo);                                          /* 释放内存 */
    free(pname);                                                /* 释放内存 */
    free(wavoffsettbl);                                         /* 释放内存 */
}

/**
 * @brief       播放某个音频文件
 * @param       fname : 文件名
 * @retval      按键值
 *   @arg       KEY0_PRES , 下一曲.
 *   @arg       KEY1_PRES , 上一曲.
 *   @arg       其他 , 错误
 */
uint8_t audio_play_song(uint8_t *fname)
{
    uint8_t res;  
    
    res = exfuns_file_type((char *)fname); 

    switch (res)
    {
        case T_WAV:
            res = wav_play_song(fname);
            break;
        case T_MP3:
            /* 自行实现 */
            break;

        default:            /* 其他文件,自动跳转到下一曲 */
            ESP_LOGI(audioplay, "can't play:%s\r\n", fname);
            res = KEY0_PRES;
            break;
    }
    return res;
}
