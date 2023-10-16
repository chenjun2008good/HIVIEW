#include <iostream>
#include <opencv2/opencv.hpp>

#include "yolov5c.h"
#include "vpss_capture.h"
#include "yolov5.h"

typedef struct {
  VpssCapture vcap;
  cv::Mat image;
  int fd;
}vcap_t;

static vcap_t vcap[YOLO_CHN_MAX];
static YOLOV5C yolov5;

//save test flag;
static int vcap_save_yuv   = 0;
static int vcap_save_image = 0;

static int vcap_cnt = 0;

int yolov5_init(int vpss_grp[YOLO_CHN_MAX], int vpss_chn[YOLO_CHN_MAX], char *ModelPath)
{
    int i = 0, cnt = 10;
    int ret = -1;
    
    while(1)
    {
      for(i = 0; i < YOLO_CHN_MAX; i++)
      {
        if(vpss_grp[i] == -1 || vpss_chn[i] == -1)
          break;
          
        if(vcap[i].fd <= 0)
        {
          //vcap[i].fd = vcap[i].vcap.init(vpss_grp[i], vpss_chn[i], 640, 640);
          vcap[i].fd = vcap[i].vcap.init(vpss_grp[i], vpss_chn[i]);
          if(vcap[i].fd > 0)
          {
            vcap_cnt++;
            printf("vcap[%d].init ok fd:%d\n", i, vcap[i].fd);
          }
          else
          {
            printf("vcap[%d].init err fd:%d\n", i, vcap[i].fd);
          }
        }
      }
      // all is ok;
      if(vcap_cnt == i)
        break;
      // timeout; 
      if(--cnt <= 0)
        break;
      // retry once;
      if(vcap_cnt > 0)
        cnt = 1;

      sleep(1);
    }
    
    printf("init all:%d, vcap_cnt:%d\n", i, vcap_cnt);
    if(vcap_cnt > 0)
    {
      ret = yolov5.init(ModelPath);
      printf("yolov5.init ret:%d [%s]\n", ret, ModelPath);
    }
    return ret;
}

int yolov5_detect(yolo_boxs_t _boxs[YOLO_CHN_MAX])
{
    static const char* class_names[] = {/*"background",*/
        "aeroplane", "bicycle", "bird", "boat",
        "bottle", "bus", "car", "cat", "chair",
        "cow", "diningtable", "dog", "horse",
        "motorbike", "person", "pottedplant",
        "sheep", "sofa", "train", "tvmonitor"
    };

    hi_video_frame_info *frame_info = NULL;
    yolo_boxs_t *boxs = _boxs;
    int ret = 0;
    int maxfd = 0;
    struct timeval to;
    fd_set read_fds;
    FD_ZERO(&read_fds);
    
    for (int i = 0; i < vcap_cnt; i++)
    {
        FD_SET(vcap[i].fd, &read_fds);
        maxfd = (maxfd < vcap[i].fd)?vcap[i].fd:maxfd;
    }
    to.tv_sec  = 2; to.tv_usec = 0;
    ret = select(maxfd + 1, &read_fds, NULL, NULL, &to);
    if (ret < 0)
    {
        printf("vpss select failed!\n");
        return -1;
    }
    else if (ret == 0)
    {
        printf("vpss get timeout!\n");
        return 0;
    }
    for (int i = 0; i < vcap_cnt; i++)
    {
      if (FD_ISSET(vcap[i].fd, &read_fds))
      {
        //printf("vpss get ok! [fd:%d]\n", vcap[i].fd);
        vcap[i].vcap.get_frame_lock(vcap[i].image, &frame_info);
        //vcap[i].vcap.get_frame(vcap[i].image);
        if(vcap[i].image.empty())
        {
            std::cout << "vpss capture failed!!!\n";
            return -1;
        }
        
        printf("vcap.get_frame_lock chn:%d, image:[%dx%d], frame_info[%dx%d]\n"
            , i, vcap[i].image.cols, vcap[i].image.rows, frame_info->video_frame.width, frame_info->video_frame.height);
        
        //cv::imwrite("test.jpg", vcap[i].image);
        //cv::Mat image = cv::imread("test.jpg");
        if(vcap_save_yuv) // save y_image .jpg
        {
          hi_video_frame *frame = &frame_info->video_frame;
          hi_char *g_user_page_addr[2] = { HI_NULL, HI_NULL }; /* 2 Y and C */
          hi_u32 g_size = 0, g_c_size = 0;
          
          char *virt_addr_y = HI_NULL;
          char *mem_content = HI_NULL;
          hi_phys_addr_t phys_addr;
          hi_pixel_format pixel_format = frame->pixel_format;
          /* When the storage format is a planar format, this variable is used to keep the height of the UV component */
          hi_u32 uv_height = 0;
          hi_bool is_uv_invert = (pixel_format == HI_PIXEL_FORMAT_YUV_SEMIPLANAR_420 ||
              pixel_format == HI_PIXEL_FORMAT_YUV_SEMIPLANAR_422) ? HI_TRUE : HI_FALSE;

          g_size = (frame->stride[0]) * (frame->height);
          if (pixel_format == HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420 || pixel_format == HI_PIXEL_FORMAT_YUV_SEMIPLANAR_420) {
              g_c_size = (frame->stride[1]) * (frame->height) / 2; /* 2 uv height */
              uv_height = frame->height / 2; /* 2 uv height */
          } else if (pixel_format == HI_PIXEL_FORMAT_YVU_SEMIPLANAR_422 ||
              pixel_format == HI_PIXEL_FORMAT_YUV_SEMIPLANAR_422) {
              g_c_size = (frame->stride[1]) * (frame->height);
              uv_height = frame->height;
          } else if (pixel_format == HI_PIXEL_FORMAT_YUV_400) {
              g_c_size = 0;
              uv_height = frame->height;
          }

          phys_addr = frame->phys_addr[0];
          g_user_page_addr[0] = (hi_char *)hi_mpi_sys_mmap(phys_addr, g_size);
          if (g_user_page_addr[0] != HI_NULL)
          {
            virt_addr_y = g_user_page_addr[0];
            /* save Y */
            cv::Mat y_image(frame->height, frame->stride[0], CV_8UC1, (unsigned char *)virt_addr_y);
            
            HI_CHAR szJpgName[128];
            sprintf(szJpgName, "/nfsroot/3403/vcap/ch%d_%dx%d_%llu.jpg", i, frame->stride[0], frame->height, frame->pts);
            cv::imwrite(szJpgName, y_image);
            fprintf(stderr, "save [%s] done!\n", szJpgName);
            fflush(stderr);
            hi_mpi_sys_munmap(g_user_page_addr[0], g_size);
          }
        }


        struct timespec ts1, ts2;
        clock_gettime(CLOCK_MONOTONIC, &ts1);

        std::vector<BoxInfo> bboxs;
        //ret = yolov5.detect(image, bboxs);  
        ret = yolov5.detect(vcap[i].image, bboxs);
        
        boxs->chn = i;
        boxs->w = vcap[i].image.cols;
        boxs->h = vcap[i].image.rows;
        boxs->size = bboxs.size();
        if(boxs->size > YOLO_BOX_MAX)
          printf("@@@@@@@@@@ chn:%d, boxs->size = %d > YOLO_BOX_MAX = %d\n", boxs->chn, boxs->size, YOLO_BOX_MAX);
        boxs->size = (boxs->size > YOLO_BOX_MAX)?YOLO_BOX_MAX:boxs->size;
        
        static char label[256][256] = {0};
        for(int i = 0; i < boxs->size; i++)
        { 
            boxs->box[i].score  = bboxs[i].score;
            boxs->box[i].x = (bboxs[i].x1 < 0)?0:(bboxs[i].x1 > boxs->w-1)?boxs->w-1:bboxs[i].x1;
            boxs->box[i].y = (bboxs[i].y1 < 0)?0:(bboxs[i].y1 > boxs->h-1)?boxs->h-1:bboxs[i].y1;
            boxs->box[i].w = (bboxs[i].x2 < 1)?1:(bboxs[i].x2 > boxs->w-0)?boxs->w-0:bboxs[i].x2;
            boxs->box[i].h = (bboxs[i].y2 < 1)?1:(bboxs[i].y2 > boxs->h-0)?boxs->h-0:bboxs[i].y2;
           
            boxs->box[i].w -= boxs->box[i].x;
            boxs->box[i].h -= boxs->box[i].y;   
            #if 0
            printf("i:%d, result[x1:%0.2f, y1:%0.2f, x2:%0.2f, y2:%0.2f] => box[x:%0.2f, y:%0.2f, w:%0.2f, h:%0.2f]\n"
                  , i
                  , bboxs[i].x1, bboxs[i].y1, bboxs[i].x2, bboxs[i].y2
                  , boxs->box[i].x, boxs->box[i].y, boxs->box[i].w, boxs->box[i].h);
                  
            assert(boxs->box[i].x + boxs->box[i].w <= boxs->w);
            assert(boxs->box[i].y + boxs->box[i].h <= boxs->h);
            #endif
            boxs->box[i].label = class_names[bboxs[i].label];
        }
        
        clock_gettime(CLOCK_MONOTONIC, &ts2);
        
        #if 1
        printf("boxs->size:%d, cost:%d ms\n", boxs->size, (ts2.tv_sec*1000 + ts2.tv_nsec/1000000) - (ts1.tv_sec*1000 + ts1.tv_nsec/1000000));
        #endif
        
        vcap[i].vcap.get_frame_unlock(frame_info);
        boxs++;
      }
    }
    
    return ((intptr_t)boxs-(intptr_t)_boxs)/sizeof(yolo_boxs_t);
}

int yolov5_deinit()
{
  yolov5.destroy();
  for(int i = 0; i < vcap_cnt; i++)
  {
    vcap[i].vcap.destroy();  
  }
  return 0;
}
 
 