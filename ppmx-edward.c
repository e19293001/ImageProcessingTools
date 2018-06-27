// Edward Sillador
// 05/16/2018
// * 05/16/2018 - implement image resize
// * 03/02/2018 - fixed crash in getNextPixel()
//                index to the input file buffer exceeds the image file size
//                getNextPixel() now returns error value.
// * 03/01/2018 - the following are changes:
// *              1. Added name and date
// *              2. Format tab width from 2 to 4
// *              3. Added argument in getNextToken(). This will return error value and
// *                 the newly added parameter current_token will be the output of the
// *                 function. 
// *              4. Return int in doProcessPPM() for error value.
// *              5. Return int in rotateGrayScaleImage() for error value.
// *              6. Added error checking in the functions:
// *                 - rotateGrayScaleImage
// *                 - putImageToFile
// *                 - getImageInfo
// *                 - doProcessPPM
// *              7. !Change! the rotation direction.
// * 12/2017 - initial version

//#define TEST_RESIZE
//#define TEST_ROTATE
#define TEST_COMARGS

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#define DATA_BUFLEN 10
#define MAX_HEADER_CHARS 128

#define PPM_MAGIC_NUM 0
#define PPM_UNSIGNED 1
#define PPM_EOF -1
#define PPM_ERROR -2

#define FILETYPE_PPM 0
#define FILETYPE_PGM 1
#define FILETYPE_PBM 2

typedef struct img_scale_info {
    int input_size;
    int output_size;
    double scale;
    int kernel_width;
    double **weights;
    int **indices;
    int P;
    int num_non_zero;
} img_scale_info;

typedef struct img_rotate_info {
    double angle;
} img_rotate_info;

typedef struct pixel {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} pixel;
  
typedef struct img_info {
    unsigned int height;
    unsigned int width;
    unsigned int new_height;
    unsigned int new_width;
    unsigned int max_color;
    unsigned int size;
    unsigned int file_type;
    pixel **buff;
    pixel **new_buff;
} img_info;

typedef struct args_flag {
    char resize_enable;
    char rotate_enable;
	char flipv_enable;
	char fliph_enable;
	char gray_enable;
	char mono_enable;
} args_flag;

// this structure is used for parsing the header file
typedef struct token{
    int kind;
    char data[DATA_BUFLEN];
    char current_char;
} token;

// contains PPM image information
typedef struct ppm_image_handler {
    img_info imginfo;
    args_flag arg_flag;
    img_scale_info scale_info;
    FILE *filep;
    unsigned char* file_buffer;
    char *filename;
    unsigned int filesize;
    unsigned int index_buffer;
    token tkn;
    img_rotate_info rotate_info;
} ppm_image_handler;

//int calc_contributions(img_scale_info *scale_info);
int calc_contributions(int in_size, int out_size, double scale, double k_width, double ***out_weights, int ***out_indices, int *contrib_size);
int mod(int a, int b);
double cubic(double x);
int init_img_scale_info(ppm_image_handler *handler);
//void release_scale_info(img_scale_info *scale_info);
//void release_scale_info(void *weights, int size);
void release_scale_info(void ***weights, int size);

int getNextToken(ppm_image_handler *handler, token *current_token);
void getNextChar(ppm_image_handler *handler);
int getNextPixel(ppm_image_handler *handler, pixel *pix);
int doProcessPPM(ppm_image_handler *handler);
int getImageInfo(ppm_image_handler *handler);
int rotateGrayScaleImage(ppm_image_handler *handler); // this can be removed
void releaseBuffer(pixel ***new_buff, unsigned int height);
int image_buff_alloc(pixel ***new_buff, unsigned int height, unsigned int width);

void usage()
{
	printf("ppmx-edward [options] (input filename)\n");
	printf("Options -fv  Flip vertically\n");
	printf("        -fh  Flip horizontally\n");
	printf("        -w(new width) Scale to the new width\n");
	printf("        -w100 means new width is 100\n");
	printf("        -r(angle)  Rotate (CW)\n");
	printf("        -r30 means rotate 30 degree CW.\n");
	printf("        -mono Convert to bilevel (.pbm) format\n");
	printf("        -gray  Convert to grayscale (.pgm) format\n");
}

int main(int argc, char *argv[])
{
	ppm_image_handler handler;
	int x;

	handler.arg_flag.rotate_enable = 0;
	handler.arg_flag.resize_enable = 0;
    handler.arg_flag.gray_enable = 0;
	handler.arg_flag.flipv_enable = 0;
	handler.arg_flag.fliph_enable = 0;
    handler.arg_flag.mono_enable = 0;
	handler.filename = NULL;

	for (x = 1; x < argc; x++)
		if (argv[x][0] == '-')
			if (argv[x][1] == 'f')
				if (argv[x][2] == 'h') handler.arg_flag.fliph_enable = 1;
				else if (argv[x][2] == 'v') handler.arg_flag.flipv_enable = 1;
				else
				{
					printf("Error: invalid option for flip.\nallowed options are -fh -fv only.\n");
					exit(0);
				}
			else if (argv[x][1] == 'w')
			{
				handler.arg_flag.resize_enable = 1;
				handler.scale_info.output_size = (int) atoi(&argv[x][2]);
			}
			else if (argv[x][1] == 'r')
			{
				handler.arg_flag.rotate_enable = 1;
				handler.rotate_info.angle = (double) atoi(&argv[x][2]);
                if (handler.rotate_info.angle < 0 || handler.rotate_info.angle >= 360)
                {
                    printf("Error: invalid option for rotate. it is less than 0 or greater than 359\n");
                    exit(0);
                }
                
				if (handler.rotate_info.angle == 0)
				{
					printf("Error: invalid option of rotate.\nallowed option is -r<degrees>.\n");
					exit(0);
				}
			}
            else if (strcmp(&argv[x][1], "gray") == 0) handler.arg_flag.gray_enable = 1;
			else if (strcmp(&argv[x][1], "mono") == 0) handler.arg_flag.mono_enable = 1;
            else
            {
                printf("Error: invalid option: %s\n", &argv[x][1]);
                usage();
                exit(0);
            }
		else handler.filename = &argv[x][0];
	if (handler.filename == NULL)
	{
		usage();
		exit(0);
	}
    if (doProcessPPM(&handler) != 0) return -1;

    return 0;
}

// write output image to file
int putImageToFile(ppm_image_handler *handler)
{
    int fileout_size =  strlen(handler->filename) + 5;
    char fileout[MAX_HEADER_CHARS];
    FILE *fofp;
    unsigned int x;
    unsigned int y;
    int error = 0;

    memset(fileout, '\0', MAX_HEADER_CHARS);
    strncpy(fileout, handler->filename, fileout_size);
    strcat(fileout, ".out");

    if (handler->imginfo.new_buff == NULL)
    {
        printf("Error: no data to write\n");
        return -1;
    }

    if ((fofp = fopen(fileout, "wb")) == NULL)
    {
        printf("Error: unable to open file for writing: %s\n", fileout);
        return -1;
    }

    if (handler->imginfo.file_type == FILETYPE_PGM)
    {
        error = fwrite("P5\n", 1, 3, fofp);
        if (error != 3)
        {
            printf("Error: failed in writing to %s\n", fileout);
            return -1;
        }
    }
    else if (handler->imginfo.file_type == FILETYPE_PBM)
    {
        error = fwrite("P4\n", 1, 3, fofp);
        if (error != 3)
        {
            printf("Error: failed in writing to %s\n", fileout);
            return -1;
        }
    }
    else
    {
        error = fwrite("P6\n", 1, 3, fofp);
        if (error != 3)
        {
            printf("Error: failed in writing to %s\n", fileout);
            return -1;
        }
    }

    sprintf(fileout, "%s", "# generated by ppmx_edward\n");
    error = fwrite(fileout, 1, strlen(fileout), fofp);
    if (error != strlen(fileout))
    {
        printf("Error: failed in writing to %s\n", fileout);
        return -1;
    }

    sprintf(fileout, "%u ", handler->imginfo.new_width);
    error = fwrite(fileout, 1, strlen(fileout), fofp);
    if (error != strlen(fileout))
    {
        printf("Error: failed in writing to %s\n", fileout);
        return -1;
    }

    sprintf(fileout, "%u\n", handler->imginfo.new_height);
    error = fwrite(fileout, 1, strlen(fileout), fofp);
    if (error != strlen(fileout))
    {
        printf("Error: failed in writing to %s\n", fileout);
        return -1;
    }

    if (!(handler->imginfo.file_type == FILETYPE_PBM))
    {
        sprintf(fileout, "%u\n", handler->imginfo.max_color);
        error = fwrite(fileout, 1, strlen(fileout), fofp);
        if (error != strlen(fileout))
        {
            printf("Error: failed in writing to %s\n", fileout);
            return -1;
        }
    }

    if (handler->imginfo.file_type == FILETYPE_PGM)
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                if ((error = fwrite(&handler->imginfo.new_buff[y][x].r,1, 1, fofp)) != 1)
                {
                    printf("Error: failed in writing to %s\n", fileout);
                    return -1;
                }
            }
    else if (handler->imginfo.file_type == FILETYPE_PBM)
    {
        unsigned char tmp = 0;
        int z = 0;
        for (y = 0; y < handler->imginfo.new_height; y++)
        {
            for (x = 0, tmp = 1, z = 0; x < (handler->imginfo.new_width); x++, tmp++)
            {
                z = z | (handler->imginfo.new_buff[y][x].r << (8 - tmp));
                if (tmp % 8 == 0)
                {
                    if ((error = fwrite(&z,1, 1, fofp)) != 1)
                    {
                        printf("Error: failed in writing to %s\n", fileout);
                        return -1;
                    }
                    tmp = 0;
                    z = 0;
                }
            }
            if (tmp-1 != 0)
                if ((error = fwrite(&z,1, 1, fofp)) != 1)
                {
                    printf("Error: failed in writing to %s\n", fileout);
                    return -1;
                }
        }
    }
    else
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                if ((error = fwrite(&handler->imginfo.new_buff[y][x].r,1, 1, fofp)) != 1)
                {
                    printf("Error: failed in writing to %s\n", fileout);
                    return -1;
                }
                if ((error = fwrite(&handler->imginfo.new_buff[y][x].g,1, 1, fofp)) != 1)
                {
                    printf("Error: failed in writing to %s\n", fileout);
                    return -1;
                }
                if ((error = fwrite(&handler->imginfo.new_buff[y][x].b,1, 1, fofp)) != 1)
                {
                    printf("Error: failed in writing to %s\n", fileout);
                    return -1;
                }
            }

    for (y = 0; y < handler->imginfo.new_height; y++)
        free(handler->imginfo.new_buff[y]);

    fclose(fofp);

    if (handler->imginfo.new_width != 0 || handler->imginfo.new_height != 0) free(handler->imginfo.new_buff);

    return 0;
}

int getNextPixel(ppm_image_handler *handler, pixel *pix)
{
    pixel ret; // can be deleted to optimize lines

    if (handler->index_buffer > handler->filesize)
    {
        printf("Error: unexpected end of file. file index: %0d exceeds file size: %0d\n", handler->index_buffer, handler->filesize);
        return -1;
    }
    ret.r = handler->file_buffer[handler->index_buffer++];
    ret.g = handler->file_buffer[handler->index_buffer++];
    ret.b = handler->file_buffer[handler->index_buffer++];

    *pix = ret;
    return 0;
}

void getNextChar(ppm_image_handler *handler)
{
    if (handler->tkn.current_char == EOF) return;

    if (handler->index_buffer < handler->filesize) handler->tkn.current_char = handler->file_buffer[handler->index_buffer++];
    else handler->tkn.current_char = EOF;

    // ignore comments
    if (handler->tkn.current_char == '#')
    {
        do
        {
            handler->tkn.current_char = handler->file_buffer[handler->index_buffer++];
        } while (handler->tkn.current_char != '\n');
        handler->tkn.current_char = '\n';
    }
}

int getNextToken(ppm_image_handler *handler, token *current_token)
{
    // returns error for any invalid token
    handler->tkn.kind = PPM_ERROR;

    // ignore spaces
    while (isspace(handler->tkn.current_char)) getNextChar(handler);

    // if token is a digit
    if (isdigit(handler->tkn.current_char))
    {
        int index = 0;
        memset(&(handler->tkn.data), '\0', DATA_BUFLEN);
        do
        {
            handler->tkn.data[index++] = handler->tkn.current_char;
            getNextChar(handler);
        } while (isdigit(handler->tkn.current_char));
        handler->tkn.kind = PPM_UNSIGNED;
    }
    else if (isalpha(handler->tkn.current_char))
    { // if token is a word
        int index = 0;
        do {
            handler->tkn.data[index++] = handler->tkn.current_char;
            getNextChar(handler);
        } while (isalnum(handler->tkn.current_char));

        handler->tkn.data[index] = '\0';

        if ((strncmp(handler->tkn.data, "P6", DATA_BUFLEN)) == 0) handler->tkn.kind = PPM_MAGIC_NUM;

        getNextChar(handler);
    }
    else return -1; // return error for anything else

    *current_token = handler->tkn;
    return 0;
}

// converts the buffer to 2 dimensional image
int getImageInfo(ppm_image_handler *handler)
{
    token current_token;
    unsigned int x;
    unsigned int y;

    // retrieve the magic number
    if (getNextToken(handler, &current_token) != 0)
    {
        printf("error in getting next token. wrong format.\n");
        return -1;
    }
    if (current_token.kind != PPM_MAGIC_NUM)
    {
        printf("error. invalid file format.\n");
        return -1;
    }
    handler->imginfo.file_type = FILETYPE_PPM;

    // retrieve the width
    if (getNextToken(handler, &current_token) != 0)
    {
        printf("error in getting next token. wrong format.\n");
        return -1;
    }
    if (current_token.kind != PPM_UNSIGNED)
    {
        printf("error. invalid file format. unable to parse width from input file.\n");
        return -1;
    }
    handler->imginfo.width = atoi(current_token.data);

    // retrieve the height
    if (getNextToken(handler, &current_token) != 0)
    {
        printf("error in getting next token. wrong format.\n");
        return -1;
    }
    if (current_token.kind != PPM_UNSIGNED)
    {
        printf("error. invalid file format. unable to parse height from input file.\n");
        return -1;
    }
    handler->imginfo.height = atoi(current_token.data);

    // retrieve the maximum color
    if (getNextToken(handler, &current_token) != 0)
    {
        printf("error in getting next token. wrong format.\n");
        return -1;
    }
    if (current_token.kind != PPM_UNSIGNED)
    {
        printf("error. invalid file format. unable to parse maximum color from input file.\n");
        return -1;
    }
    handler->imginfo.max_color = atoi(current_token.data);
  
    handler->imginfo.buff = (pixel **) malloc(handler->imginfo.height * sizeof(pixel *));
    if (handler->imginfo.buff == NULL)
    {
        printf("error. can not allocate memory\n");
        return -1;
    }
  
    handler->imginfo.size = handler->imginfo.height * handler->imginfo.width;
    for (y = 0; y < handler->imginfo.height; y++)
    {
        handler->imginfo.buff[y] = (pixel *) malloc(handler->imginfo.width * sizeof(pixel));
        if (handler->imginfo.buff[y] == NULL)
        {
            printf("error. can not allocate memory\n");
            return -1;
        }
        for (x = 0; x < handler->imginfo.width; x++)
            if (getNextPixel(handler, &handler->imginfo.buff[y][x]) != 0) return -1;
    }
    handler->imginfo.new_width = 0;
    handler->imginfo.new_height = 0;

    if (handler->filesize != handler->index_buffer)
    {
        printf("file format error\n");
        return -1;
    }

    return 0;
}

void releaseBuffer(pixel ***new_buff, unsigned int height)
{
    unsigned int y;
    for (y = 0; y < height; y++)
        free((*new_buff)[y]);

    free(*new_buff);
}

double cubic(double x)
{
    double ret = 0;
    double absx = fabs(x);
    double absx2 = absx * absx;
    double absx3 = absx2 * absx;

    if (absx <= 1) ret = (1.5*absx3) - (2.5*absx2) + 1;

    if ((1 < absx) && (absx <= 2)) ret = ret + ((-0.5*absx3) + (2.5*absx2) - (4*absx) + 2);
            
    return ret;
}

int mod(int a, int b)
{
    int r = 0;
    if (b != 0) r = a % b;
    return r < 0 ? r + b : r;
}

int calc_contributions(int in_size, int out_size, double scale, double k_width, double ***out_weights, int ***out_indices, int *contrib_size)
{
    int x = 0;
    int y = 0;
    unsigned int aux_size = in_size * 2;
    int *aux;
    int **indices;
    double **weights;
    int P = 0;
    int num_non_zero = 0;
    int ind_w_ptr_x = 0;
    unsigned char *ind2store;

    if (scale < 1.0) k_width = k_width / scale;

    P = (int) ceil(k_width) + 2;

    if ((indices = (int **) malloc(out_size * sizeof(int*))) == NULL)
    {
        printf("error. allocating indices\n");
        return -1;
    }

    if ((weights = (double **) malloc(out_size * sizeof(double*))) == NULL)
    {
        printf("error. allocating indices\n");
        return -1;
    }

    //if (indices == NULL || weights == NULL)
    //{
    //    printf("error. allocating indices\n");
    //    return - 1;
    //}

    for (y = 0; y < out_size; y++)
    {
        indices[y] = (int *) malloc(P * sizeof(double));
        weights[y] = (double *) malloc(P * sizeof(double));

        if (indices[y] == NULL || weights[y] == NULL)
        {
            printf("fatal. allocating memory for weights and indices");
            return -1;
        }
    }

    if ((aux = (int *) malloc(aux_size * (sizeof(int)))) == NULL)
    {
        printf("fatal. allocating memory for aux\n");
        return - 1;
    }            

    memset(aux, 0, aux_size);

    for (x = 0, y = 0; x < in_size; x++)
        aux[y++] = x;

    for (x = in_size-1; x >= 0; x--)
        aux[y++] = x;

    // generate indices
    for (y = 0; y < out_size; y++)
        for (x = 0; x < P; x++)
        {
            // generate an array from 1 to output_size
            // divide each element to scale
            double u = ((y+1) / scale) + (0.5 * (1 - (1 / scale)));
            indices[y][x] = floor(u - (k_width / 2)) + (x - 1);
        }

    // generate weights
    if (scale < 1.0)
        for (y = 0; y < out_size; y++)
            for (x = 0; x < P; x++)
            {
                double u = ((y+1) / scale) + (0.5 * (1 - (1 / scale)));
                double t = cubic((u - (double)indices[y][x] - 1) * scale);
                weights[y][x] = scale * t;
            }
    else
        for (y = 0; y < out_size; y++)
            for (x = 0; x < P; x++)
            {
                double u = ((y+1) / scale) + (0.5 * (1 - (1 / scale)));
                weights[y][x] = cubic(u - (double)indices[y][x] - 1);
            }

    for (y = 0; y < out_size; y++)
    {
        double sum = 0.0;
        for (x = 0; x < P; x++) sum += weights[y][x];
        for (x = 0; x < P; x++) weights[y][x] = weights[y][x] / sum;
    }

    for (y = 0; y < out_size; y++)
        for (x = 0; x < P; x++)
            indices[y][x] = aux[mod(((int) indices[y][x]),aux_size)];

    for (y = 0; y < out_size; y++)
    {
        int num_non_zero_t = 0;
        for (x = 0; x < P; x++)
            if (weights[y][x] != 0.0f)
            {
                num_non_zero_t = num_non_zero_t + 1;
            }
        if (num_non_zero < num_non_zero_t) num_non_zero = num_non_zero_t;
    }

    ind2store = (unsigned char*) malloc(P * sizeof(unsigned char*));

    for (x = 0; x < P; x++) ind2store[x] = 0;

    for (y = 0; y < out_size; y++)
        for (x = 0; x < P; x++)
            if (weights[y][x] != 0.0f) ind2store[x] = 1;

    (*out_indices) = (int **) malloc(out_size * sizeof(int*));
    (*out_weights) = (double **) malloc(out_size * sizeof(double*));
    
    for (y = 0; y < out_size; y++)
    {
        (*out_indices)[y] = (int *) malloc(num_non_zero * sizeof(int));
        (*out_weights)[y] = (double *) malloc(num_non_zero * sizeof(double));
    }

    for (y = 0; y < out_size; y++)
    {
        ind_w_ptr_x = 0;
        for (x = 0; x < P; x++)
        {
            if (ind2store[x])
            {
                (*out_indices)[y][ind_w_ptr_x] = indices[y][x];
                (*out_weights)[y][ind_w_ptr_x] = weights[y][x];
                ind_w_ptr_x = ind_w_ptr_x + 1;
            }
        }
    }

    *contrib_size = num_non_zero; // TODO: num_non_zero can be removed

    for (y = 0; y < out_size; y++)
    {
        free(indices[y]);
        free(weights[y]);
    }
	
    free(ind2store);
    free(weights);
    free(indices);
    free(aux);

    return 0;
}

void release_scale_info(void ***weights, int size)
{
    int y = 0;
    for (y = 0; y < size; y++)
    {
        free((*weights)[y]);
    }
    free(*weights);
}

double getScaleFromSize(int in_size, int out_size)
{
    return ((double) out_size) / in_size;
}

// TODO: can be removed
int getSizeFromScale(int in_size, double scale)
{
    return (int) ((double)scale * in_size);
}

int init_img_scale_info(ppm_image_handler *handler)
{
    // width
    handler->scale_info.kernel_width = 4.0;
    handler->scale_info.P = handler->scale_info.kernel_width + 2;
    handler->scale_info.input_size = handler->imginfo.width;
    if ((int) (handler->imginfo.new_width = handler->scale_info.output_size) < 1)
    {
        printf("invalid option for width: %0d\n", handler->imginfo.new_width);
        return -1;
    }
    handler->scale_info.scale = getScaleFromSize(handler->scale_info.input_size, handler->scale_info.output_size);

    // height
    handler->imginfo.new_height = getSizeFromScale(handler->imginfo.height, handler->scale_info.scale);

    return 0;
}

double to_radians(double degrees) {
	return (degrees * M_PI)/180.0;
}

double to_degrees(double radians) {
    return (radians * 180.0) / M_PI;
}

void calc_rot_size(double angle,
				   unsigned int old_width, unsigned int old_height,
				   unsigned int *new_width, unsigned int *new_height)
{
    double theta1; // TODO: minimize lines here

    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
	theta1 = to_radians(angle);
	d = old_height;
	c = old_width;

	b = c * sin(theta1);
	a = c * cos(theta1);
	f = d * sin(theta1);
	e = d * cos(theta1);
	*new_width = round(a + f);
	*new_height = round(b + e);
}	

int rotate(ppm_image_handler *handler)
{
    int x;
    int y;
    int x_center_in;
    int y_center_in;

	double angle;

	int x_offset;
	int y_offset;

	angle = handler->rotate_info.angle;

	if (angle >= 270) angle = 360 - angle;
	else if (angle > 180) angle = angle - 180;
	else if (angle > 90) angle = 180 - angle;

	calc_rot_size(angle, handler->imginfo.width, handler->imginfo.height, &handler->imginfo.new_width, &handler->imginfo.new_height);
	angle = to_radians(handler->rotate_info.angle);
	
	x_center_in = floor(handler->imginfo.width / 2);
	y_center_in = floor(handler->imginfo.height / 2);

	x_offset = floor(handler->imginfo.new_width / 2) - floor(handler->imginfo.width / 2);
	y_offset = floor(handler->imginfo.new_height / 2) - floor(handler->imginfo.height / 2);

    if (image_buff_alloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == -1) return -1;

    for (y = 0; y < handler->imginfo.new_height; y++) memset(handler->imginfo.new_buff[y], 0x00, handler->imginfo.new_width * sizeof(pixel));

    printf("width: %0d\n", handler->imginfo.width);
    printf("height: %0d\n", handler->imginfo.height);
    printf("new_width: %0d\n", handler->imginfo.new_width);
    printf("new_height: %0d\n", handler->imginfo.new_height);

    handler->imginfo.new_buff[handler->imginfo.new_height-1][handler->imginfo.new_width-1].r = 0xFF;
    handler->imginfo.new_buff[handler->imginfo.new_height-1][handler->imginfo.new_width-1].g = 0x00;
    handler->imginfo.new_buff[handler->imginfo.new_height-1][handler->imginfo.new_width-1].b = 0x00;

	for (y = 0; y < handler->imginfo.new_height; y++)
    {
        for (x = 0; x < handler->imginfo.new_width; x++)
        {
            double newX; // TODO: make this smaller
            double newY;
			int xx;
			int yy;

            int x0;
            int y0;

            double nX;
            double nY;

			xx = x - x_offset;
			yy = y - y_offset;
			x0 = xx - x_center_in;
			y0 = yy - y_center_in;

            newX = (cos(angle) * (double) (x0)) + (sin(angle) * (double) (y0));
            newY = -(sin(angle) * (double)(x0)) + (cos(angle) * (double) (y0));

            nX = round(newX + x_center_in);
            nY = round(newY + y_center_in);

			if (((ceil(nX) < handler->imginfo.width) && (ceil(nY) < handler->imginfo.height) && (nY >= 0) && (nX >= 0)))
            {
                double q_r = 0.0f;
                double q_g = 0.0f;
                double q_b = 0.0f;
                int j;
                int i;

                if (nX > 1 && nY > 1 && nX < handler->imginfo.width - 2 && nY < handler->imginfo.height - 2)
                {
                    for (j = 0; j < 4; j++) 
                    {
                        int v = ceil(nY) - 1 + j;
                        double p_r = 0.0f;
                        double p_g = 0.0f;
                        double p_b = 0.0f;
                        for (i = 0; i < 4; i++)
                        {
                            int u = ceil(nX) - 1 + i;
                            p_r += (handler->imginfo.buff[v][u].r * cubic(nX - u));
                            p_g += (handler->imginfo.buff[v][u].g * cubic(nX - u));
                            p_b += (handler->imginfo.buff[v][u].b * cubic(nX - u));
                        }

                        q_r += p_r * cubic(nY - v);
                        q_g += p_g * cubic(nY - v);
                        q_b += p_b * cubic(nY - v);
                    }

                    if (q_r < 0) q_r = 0.0f;
                    if (q_g < 0) q_g = 0.0f;
                    if (q_b < 0) q_b = 0.0f;
                    
                    if (q_r >= 256) q_r = 255.0f;
                    if (q_g >= 256) q_g = 255.0f;
                    if (q_b >= 256) q_b = 255.0f;

                    handler->imginfo.new_buff[y][x].r = (int) q_r;
                    handler->imginfo.new_buff[y][x].g = (int) q_g;
                    handler->imginfo.new_buff[y][x].b = (int) q_b;
                }
                else
                {
                    handler->imginfo.new_buff[y][x] = handler->imginfo.buff[(int)nY][(int)nX];
                }
            }
        }
    }

    return 0;
}
        
int imresize(ppm_image_handler *handler, int out_size, int dim, double **weights, int **indices, int weights_sz)
{
    int x;
    int y;
    int z;
    
    if (dim == 0) // scale height
    {
        handler->imginfo.new_height = out_size;
        handler->imginfo.new_width = handler->imginfo.width;

        if (image_buff_alloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.width) == -1) return -1;
  
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                double sum_r = 0.0f;
                double sum_g = 0.0f;
                double sum_b = 0.0f;

                for (z = 0; z < weights_sz; z++)
                {
                    sum_r = sum_r + (handler->imginfo.buff[indices[y][z]][x].r * weights[y][z]);
                    sum_g = sum_g + (handler->imginfo.buff[indices[y][z]][x].g * weights[y][z]);
                    sum_b = sum_b + (handler->imginfo.buff[indices[y][z]][x].b * weights[y][z]);
                }
                sum_r = round(sum_r);
                sum_g = round(sum_g);
                sum_b = round(sum_b);

                handler->imginfo.new_buff[y][x].r = (sum_r < 0.0f) ? 0.0f : (sum_r >= 256) ? 255.0f : (int) sum_r;
                handler->imginfo.new_buff[y][x].g = (sum_g < 0.0f) ? 0.0f : (sum_g >= 256) ? 255.0f : (int) sum_g;
                handler->imginfo.new_buff[y][x].b = (sum_b < 0.0f) ? 0.0f : (sum_b >= 256) ? 255.0f : (int) sum_b;
            }
    }
    else
    {
        handler->imginfo.new_height = handler->imginfo.height;
        handler->imginfo.new_width = out_size;

        if (image_buff_alloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == -1) return -1;
  
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                double sum_r = 0.0f;
                double sum_g = 0.0f;
                double sum_b = 0.0f;

                for (z = 0; z < weights_sz; z++)
                {
                    sum_r = sum_r + handler->imginfo.buff[y][indices[x][z]].r * weights[x][z];
                    sum_g = sum_g + handler->imginfo.buff[y][indices[x][z]].g * weights[x][z];
                    sum_b = sum_b + handler->imginfo.buff[y][indices[x][z]].b * weights[x][z];
                }

                sum_r = round(sum_r);
                sum_g = round(sum_g);
                sum_b = round(sum_b);

                handler->imginfo.new_buff[y][x].r = (sum_r < 0.0f) ? 0.0f : (sum_r >= 256) ? 255.0f : (int) sum_r;
                handler->imginfo.new_buff[y][x].g = (sum_g < 0.0f) ? 0.0f : (sum_g >= 256) ? 255.0f : (int) sum_g;
                handler->imginfo.new_buff[y][x].b = (sum_b < 0.0f) ? 0.0f : (sum_b >= 256) ? 255.0f : (int) sum_b;
            }
    }
    
    return 0;
}

// flip_direction - 1 vertical
//                - 0 horizontal
int flip(ppm_image_handler *handler, unsigned char flip_direction)
{
    int x;
    int y;
    pixel tmp;

    handler->imginfo.new_height = handler->imginfo.height;
    handler->imginfo.new_width = handler->imginfo.width;

    handler->imginfo.new_buff = handler->imginfo.buff;

    if (flip_direction) // flip vertical
        for (y = 0; y < handler->imginfo.new_height/2; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                tmp = handler->imginfo.new_buff[y][x];
                handler->imginfo.new_buff[y][x] = handler->imginfo.new_buff[((handler->imginfo.new_height)-1) - y][x];
                handler->imginfo.new_buff[((handler->imginfo.new_height)-1) - y][x] = tmp;
            }
    else
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width/2; x++)
            {
                tmp = handler->imginfo.new_buff[y][x];
                handler->imginfo.new_buff[y][x] = handler->imginfo.new_buff[y][((handler->imginfo.new_width)-1)-x];
                handler->imginfo.new_buff[y][((handler->imginfo.new_width)-1) - x] = tmp;
            }
    return 0;
}

int image_buff_alloc(pixel ***new_buff, unsigned int height, unsigned int width)
{
    int y;

    if (((*new_buff) = (pixel **) malloc(height * sizeof(pixel *))) == NULL) return -1;

    for (y = 0; y < height; y++)
        if (((*new_buff)[y] = (pixel *) malloc(width * sizeof(pixel))) == NULL) return -1;

    return 0;
}

int mono(ppm_image_handler *handler) // TODO: should return error
{
    int x;
    int y;

    double matrix[] = {0.1250, 1.0000, 0.1875, 0.8125, 0.6250, 0.3750, 0.6875, 0.4375, 0.2500, 0.8750, 0.0625, 0.9375, 0.7500, 0.5000, 0.5625, 0.3125};

    handler->imginfo.file_type = FILETYPE_PBM;
    handler->imginfo.new_height = handler->imginfo.height;
    handler->imginfo.new_width = handler->imginfo.width;

    if (image_buff_alloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == -1) return -1;

// bayer 4x4  
// ------------------------------------------------------------------------------------------------------------------------------------------------------
    for (y = 0; y < handler->imginfo.new_height; y++)
    {
        for (x = 0; x < handler->imginfo.new_width; x++)
        {
            unsigned char oldpixel = (handler->imginfo.buff[y][x].r + handler->imginfo.buff[y][x].g + handler->imginfo.buff[y][x].b) / 3;
            if (oldpixel >= matrix[(x % 4) * 4 + (y % 4)] * 255)
                handler->imginfo.new_buff[y][x].r = 0;
            else
                handler->imginfo.new_buff[y][x].r = 1;
        }
    }
// ------------------------------------------------------------------------------------------------------------------------------------------------------
    return 0;
}

int gray(ppm_image_handler *handler) // TODO: should return error
{
    int x;
    int y;

    handler->imginfo.file_type = FILETYPE_PGM;
    handler->imginfo.new_height = handler->imginfo.height;
    handler->imginfo.new_width = handler->imginfo.width;

    if (image_buff_alloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == -1) return -1;

    for (y = 0; y < handler->imginfo.new_height; y++)
        for (x = 0; x < handler->imginfo.new_width; x++)
        {
            handler->imginfo.new_buff[y][x].r = (unsigned char)((handler->imginfo.buff[y][x].r + handler->imginfo.buff[y][x].g + handler->imginfo.buff[y][x].b) / 3);
        }


    return 0;
}

int doProcessPPM(ppm_image_handler *handler)
{
    int retsz;

    // initialize ppm_image_handler
    handler->filep = fopen(handler->filename, "rb");
    if (handler->filep == NULL)
    {
        printf("error. can not open file: %s\n", handler->filename);
        return -1;
    }

    if (fseek(handler->filep , 0 , SEEK_END) < 0)
    {
        printf("error. can not set file position in fseek.\n");
        return -1;
    }
    
    // get file size and allocate buffer for the whole file
    handler->filesize = ftell(handler->filep);
    rewind(handler->filep);
    handler->file_buffer = (unsigned char*) malloc(sizeof(unsigned char) * handler->filesize);
    if (handler->file_buffer == NULL)
    {
        printf("error. can not allocate memory\n");
        return -1;
    }
    retsz = fread(handler->file_buffer, 1, handler->filesize, handler->filep);
    if (retsz != handler->filesize)
    {
        printf("error in reading input file.\n");
        return -1;
    }

    // initialize variables before parsing
    handler->index_buffer = 0;
    handler->tkn.current_char = '\n';

    // get header file information
    if (getImageInfo(handler) != 0)
    {
        free(handler->file_buffer);
        fclose(handler->filep);
        return -1;
    }
  
    handler->imginfo.new_buff = NULL;

    if (handler->arg_flag.resize_enable)
    {
        double **weights[2];
        int **indices[2];
        int weights_sz[2];
        int im_sz[2];
        double scale[2];
        int order[2];

        printf("resize\n");

        if (init_img_scale_info(handler) == -1)
        {
            return -1;
        }

        scale[0] = getScaleFromSize(handler->imginfo.height, handler->imginfo.new_height);
        scale[1] = getScaleFromSize(handler->imginfo.width, handler->imginfo.new_width);

        if (scale[0] < scale[1])
        {
            order[0] = 0;
            order[1] = 1;
        }
        else
        {
            order[0] = 1;
            order[1] = 0;
        }

        if (calc_contributions(handler->imginfo.height, handler->imginfo.new_height, scale[0], 4.0, &weights[0], &indices[0], &weights_sz[0]) == -1) return -1;
        if (calc_contributions(handler->imginfo.width, handler->imginfo.new_width, scale[1], 4.0, &weights[1], &indices[1], &weights_sz[1]) == -1) return -1;

        im_sz[0] = handler->imginfo.new_height;
        im_sz[1] = handler->imginfo.new_width;

        int x, y;
        int out_width = handler->imginfo.new_width;
        int dim = order[0];
        imresize(handler, im_sz[dim], dim, weights[dim], indices[dim], weights_sz[dim]);
        releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);
        handler->imginfo.buff = handler->imginfo.new_buff;
        handler->imginfo.new_width = out_width;
        handler->imginfo.height = handler->imginfo.new_height;
        handler->imginfo.width = handler->imginfo.new_width;
        dim = order[1];
        imresize(handler, im_sz[dim], dim, weights[dim], indices[dim], weights_sz[dim]);

        for (x = 0; x < 2; x++)
        {
            for (y = 0; y < im_sz[x]; y++)
            {
                free((weights[x])[y]);
                free((indices[x])[y]);
            }
            free(weights[x]);
            free(indices[x]);
        }
    }

    if (handler->arg_flag.rotate_enable)
    {
        printf("rotate\n");
        if (handler->arg_flag.resize_enable) // TODO: this is ugly
        {
            releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);
            handler->imginfo.buff = handler->imginfo.new_buff;
            handler->imginfo.height = handler->imginfo.new_height;
            handler->imginfo.width = handler->imginfo.new_width;
        }
        rotate(handler);
    }

    if (handler->arg_flag.gray_enable)
    {
        printf("gray\n");
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable) // TODO: this is ugly
        {
            releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);
            handler->imginfo.buff = handler->imginfo.new_buff;
            handler->imginfo.height = handler->imginfo.new_height;
            handler->imginfo.width = handler->imginfo.new_width;
        }
        gray(handler);
    }
        
    if (handler->arg_flag.mono_enable)
    {
        printf("mono\n");
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable) // TODO: this is ugly
        {
            releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);
            handler->imginfo.buff = handler->imginfo.new_buff;
            handler->imginfo.height = handler->imginfo.new_height;
            handler->imginfo.width = handler->imginfo.new_width;
        }
        mono(handler);
    }

	if (handler->arg_flag.flipv_enable)
    {
        printf("flipv\n");
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable) // TODO: this is ugly
        {
            releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);
            handler->imginfo.buff = handler->imginfo.new_buff;
            handler->imginfo.height = handler->imginfo.new_height;
            handler->imginfo.width = handler->imginfo.new_width;
        }
        flip(handler,1);
    }
        
	if (handler->arg_flag.fliph_enable)
    {
        printf("fliph\n");
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable) // TODO: this is ugly
        {
            releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);
            handler->imginfo.buff = handler->imginfo.new_buff;
            handler->imginfo.height = handler->imginfo.new_height;
            handler->imginfo.width = handler->imginfo.new_width;
        }
        flip(handler,0);
    }
        
    // write the processed image to file
    if (putImageToFile(handler) != 0)
    {
        free(handler->file_buffer);
        fclose(handler->filep);
        releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);
        return -1;
    }

    // buffer is reused for flipv and fliph
    if (!(handler->arg_flag.flipv_enable || handler->arg_flag.fliph_enable)) releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);

    // release allocated image buffer
    free(handler->file_buffer);
    fclose(handler->filep);
    return 0;
}
