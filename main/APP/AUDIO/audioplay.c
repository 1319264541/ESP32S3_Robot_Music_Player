/**
 ****************************************************************************************************
 * @file        audioplay.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2024-06-25
 * @brief       ���ֲ����� Ӧ�ô���
 * @license     Copyright (c) 2020-2032, �������������ӿƼ����޹�˾
 ****************************************************************************************************
 * @attention
 *
 * ʵ��ƽ̨:����ԭ�� ESP32S3 BOX ������
 * ������Ƶ:www.yuanzige.com
 * ������̳:www.openedv.com
 * ��˾��ַ:www.alientek.com
 * �����ַ:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "audioplay.h"


const char* audioplay = "audioplay";

extern uint8_t uart_config1,pause_config1,music_key1; // ���ڿ��Ʊ�־λ

__audiodev g_audiodev;          /* ���ֲ��ſ����� */

/**
 * @brief       ��ʼ��Ƶ����
 * @param       ��
 * @retval      ��
 */
void audio_start(void)
{
    g_audiodev.status = 3 << 0; /* ��ʼ����+����ͣ */
    i2s_trx_start();
}

/**
 * @brief       ֹͣ��Ƶ����
 * @param       ��
 * @retval      ��
 */
void audio_stop(void)
{
    es8311_set_voice_mute(1);   /* 先静音DAC，防止I2S停止后DAC输出DC电平导致扬声器过热和电源跌落 */
    vTaskDelay(pdMS_TO_TICKS(10));
    g_audiodev.status = 0;
    i2s_trx_stop();
}

/**
 * @brief       �õ�path·���£�Ŀ���ļ�������
 * @param       path : �ļ�·��
 * @retval      ��Ч�ļ�����
 */
uint16_t audio_get_tnum(uint8_t *path)
{
    uint8_t res;
    uint16_t rval = 0;
    FF_DIR tdir;                                                /* ��ʱĿ¼ */
    FILINFO *tfileinfo;                                         /* ��ʱ�ļ���Ϣ */
    
    tfileinfo = (FILINFO*)malloc(sizeof(FILINFO));              /* �����ڴ� */
    
    res = f_opendir(&tdir, (const TCHAR*)path);                 /* ��Ŀ¼ */
    
    if ((res == FR_OK) && tfileinfo)
    {
        while (1)                                               /* ��ѯ�ܵ���Ч�ļ��� */
        {
            res = f_readdir(&tdir, tfileinfo);                  /* ��ȡĿ¼�µ�һ���ļ� */
            
            if ((res != FR_OK) || (tfileinfo->fname[0] == 0))
            {
                break;                                          /* ������/��ĩβ��,�˳� */
            }

            res = exfuns_file_type(tfileinfo->fname);
            
            if ((res & 0xF0) == 0x40)                           /* ȡ����λ,�����ǲ��������ļ� */
            {
                rval++;                                         /* ��Ч�ļ�������1 */
            }
        }
    }
    
    free(tfileinfo);                                            /* �ͷ��ڴ� */
    
    return rval;
}

/**
 * @brief       ��ʾ��Ŀ����
 * @param       index : ��ǰ����
 * @param       total : ���ļ���
 * @retval      ��
 */
void audio_index_show(uint16_t index, uint16_t total)
{
    /* ��ʾ��ǰ��Ŀ������,������Ŀ�� */
    //lcd_show_num(30 + 0, 230, index, 3, 16, RED);   /* ���� */
    //lcd_show_char(30 + 24, 230, '/', 16, 0, RED);
    //lcd_show_num(30 + 32, 230, total, 3, 16, RED);  /* ����Ŀ */
}

/**
 * @brief       ��ʾ����ʱ��,������ ��Ϣ
 * @param       totsec : ��Ƶ�ļ���ʱ�䳤��
 * @param       cursec : ��ǰ����ʱ��
 * @param       bitrate: ������(λ��)
 * @retval      ��
 */
void audio_msg_show(uint32_t totsec, uint32_t cursec, uint32_t bitrate)
{
    static uint16_t playtime = 0xFFFF;                                  /* ��ʱ���� */
    
    if (playtime != cursec)                                             /* ��Ҫ������ʾʱ�� */
    {
        playtime = cursec;
        
        /* ��ʾ����ʱ�� */
        //lcd_show_xnum(30, 210, playtime / 60, 2, 16, 0X80, RED);        /* ���� */
        //lcd_show_char(30 + 16, 210, ':', 16, 0, RED);
        //lcd_show_xnum(30 + 24, 210, playtime % 60, 2, 16, 0X80, RED);   /* ���� */
        //lcd_show_char(30 + 40, 210, '/', 16, 0, RED);
        
        /* ��ʾ��ʱ�� */
        //lcd_show_xnum(30 + 48, 210, totsec / 60, 2, 16, 0X80, RED);     /* ���� */
        //lcd_show_char(30 + 64, 210, ':', 16, 0, RED);
        //lcd_show_xnum(30 + 72, 210, totsec % 60, 2, 16, 0X80, RED);     /* ���� */
        
        /* ��ʾλ�� */
        //lcd_show_num(30 + 110, 210, bitrate / 1000, 4, 16, RED);/* ��ʾλ�� */
        //lcd_show_string(30 + 110 + 32 , 210, 200, 16, 16, "Kbps", RED);
    }
}

/**
 * @brief       ת��
 * @param       fs:�ļ�ϵͳ����
 * @param       clst:ת��
 * @retval      =0:�����ţ�0:ʧ��
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
 * @brief       ƫ��
 * @param       dp:ָ��Ŀ¼����
 * @param       Offset:Ŀ¼����ƫ����
 * @retval      FR_OK(0):�ɹ���!=0:����
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
 * @brief       ��������
 * @param       ��
 * @retval      ��
 */
void audio_play(void)
{
    uint8_t res;
    FF_DIR wavdir;                                              /* Ŀ¼ */
    FILINFO *wavfileinfo;                                       /* �ļ���Ϣ */
    uint8_t *pname;                                             /* ��·�����ļ��� */
    uint16_t totwavnum;                                         /* �����ļ����� */
    uint16_t curindex;                                          /* ��ǰ���� */
    uint8_t key;                                                /* ��ֵ */
    uint32_t temp;
    uint32_t *wavoffsettbl;                                     /* ����offset������ */

    while (f_opendir(&wavdir, "0:/MUSIC"))                      /* �������ļ��� */
    {
        //text_show_string(30, 190, 240, 16, "MUSIC�ļ��д���!", 16, 0, BLUE);
        vTaskDelay(200);
        //lcd_fill(30, 190, 240, 206, WHITE);                     /* �����ʾ */
        vTaskDelay(200);
    }

    totwavnum = audio_get_tnum((uint8_t *)"0:/MUSIC");          /* �õ�����Ч�ļ��� */
    
    while (totwavnum == 0)                                      /* �����ļ�����Ϊ0 */
    {
        //text_show_string(30, 190, 240, 16, "û�������ļ�!", 16, 0, BLUE);
        vTaskDelay(200);
        //lcd_fill(30, 190, 240, 146, WHITE);                     /* �����ʾ */
        vTaskDelay(200);
    }
    
    wavfileinfo = (FILINFO*)malloc(sizeof(FILINFO));            /* �����ڴ� */
    pname = malloc(255 * 2 + 1);                                /* Ϊ��·�����ļ��������ڴ� */
    wavoffsettbl = malloc(4 * totwavnum);                       /* ����4*totwavnum���ֽڵ��ڴ�,���ڴ�������ļ�off block���� */
    
    while (!wavfileinfo || !pname || !wavoffsettbl)             /* �ڴ������� */
    {
        //text_show_string(30, 190, 240, 16, "�ڴ����ʧ��!", 16, 0, BLUE);
        vTaskDelay(200);
        //lcd_fill(30, 190, 240, 146, WHITE);               /* �����ʾ */
        vTaskDelay(200);
    }
    
    /* ��¼���� */
    res = f_opendir(&wavdir, "0:/MUSIC");                       /* ��Ŀ¼ */
    
    if (res == FR_OK)
    {
        curindex = 0;                                           /* ��ǰ����Ϊ0 */
        
        while (1)                                               /* ȫ����ѯһ�� */
        {
            temp = wavdir.dptr;                                 /* ��¼��ǰindex */

            res = f_readdir(&wavdir, wavfileinfo);              /* ��ȡĿ¼�µ�һ���ļ� */
            
            if ((res != FR_OK) || (wavfileinfo->fname[0] == 0))
            {
                break;                                          /* ������/��ĩβ��,�˳� */
            }

            res = exfuns_file_type(wavfileinfo->fname);
            
            if ((res & 0xF0) == 0x40)                           /* ȡ����λ,�����ǲ��������ļ� */
            {
                wavoffsettbl[curindex] = temp;                   /* ��¼���� */
                curindex++;
            }
        }
    }
    
    curindex = 0;                                               /* ��0��ʼ��ʾ */
    res = f_opendir(&wavdir, (const TCHAR*)"0:/MUSIC");         /* ��Ŀ¼ */
    
    while (res == FR_OK)                                        /* �򿪳ɹ� */
    {
        atk_dir_sdi(&wavdir, wavoffsettbl[curindex]);               /* �ı䵱ǰĿ¼���� */
        res = f_readdir(&wavdir, wavfileinfo);                  /* ��ȡĿ¼�µ�һ���ļ� */
        
        if ((res != FR_OK) || (wavfileinfo->fname[0] == 0))
        {
            break;                                              /* ������/��ĩβ��,�˳� */
        }
        
        strcpy((char *)pname, "0:/MUSIC/");                     /* ����·��(Ŀ¼) */
        strcat((char *)pname, (const char *)wavfileinfo->fname);/* ���ļ������ں��� */
        //lcd_fill(30, 190, lcd_dev.width, lcd_dev.height, WHITE); /* ���֮ǰ����ʾ */
        //audio_index_show(curindex + 1, totwavnum);
        //text_show_string(30, 190, lcd_dev.width - 60, 16, (char *)wavfileinfo->fname, 16, 0, BLUE);   /* ��ʾ�������� */
        
		key = audio_play_song(pname);                           /* ���������Ƶ�ļ� */

		if(uart_config1 == 1) // ������ڿ��Ʊ�־λ�����ã����ȴ�������ָ��
		{
			uart_config1 = 0; // ���ô��ڿ��Ʊ�־λ
			    // ? ֻ�޸����У�������и�ָ���/���ף����Ÿ�ֵ�����ƻ�ԭ���߼�
    		if(music_key1 == KEY0_PRES || music_key1 == KEY1_PRES)
    		{
        		key = music_key1;
    		}
			// key = music_key1; // ��ȡ����ָ�����/��ͣ/��һ���ȣ�
			music_key1 = 0; // �������ֿ��Ƽ�ֵ
		}

        if (key == KEY1_PRES)                                   /* ��һ�� */
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
        else if (key == KEY0_PRES)                              /* ��һ�� */
        {
            curindex++;

            if (curindex >= totwavnum)
            {
                curindex = 0;                                   /* ��ĩβ��ʱ��,�Զ���ͷ��ʼ */
            }
        }
        else
        {
            break;                                              /* �����˴��� */
        }
    }

    free(wavfileinfo);                                          /* �ͷ��ڴ� */
    free(pname);                                                /* �ͷ��ڴ� */
    free(wavoffsettbl);                                         /* �ͷ��ڴ� */
}

/**
 * @brief       ����ĳ����Ƶ�ļ�
 * @param       fname : �ļ���
 * @retval      ����ֵ
 *   @arg       KEY0_PRES , ��һ��.
 *   @arg       KEY1_PRES , ��һ��.
 *   @arg       ���� , ����
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
            /* ����ʵ�� */
            break;

        default:            /* �����ļ�,�Զ���ת����һ�� */
            ESP_LOGI(audioplay, "can't play:%s\r\n", fname);
            res = KEY0_PRES;
            break;
    }
    return res;
}
