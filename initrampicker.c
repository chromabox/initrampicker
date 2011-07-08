/*
   initram picker
    initramfs(cpio) taken from kernel zImage
    written by chromabox
*/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

static const char* VERSTR="1.01.00";

static const char gzip_signature[3] =  {0x1F,0x8B,0x08};
static const char lzo_signature[9]  =  {0x89,0x4C,0x5A,0x4F,0x00,0x0D,0x0A,0x1A,0x0A};
// lzma -9 stream (XX = compress type 00 02 = -9. FF-FF: lzma stream type)
static const char lzma_signature[13] = {0x5D,0x00,0x00,0x00,0x02,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static const char cpio_signature[5] =  {0x30,0x37,0x30,0x37,0x30};	// 07070



static const char trailer_signature[11] = {0x54,0x52,0x41,0x49,0x4C,0x45,0x52,0x21,0x21,0x21,0x00}; // TRAILER!!!

static const char *gzip_ext= "gz";
static const char *lzo_ext= "lzo";
static const char *lzma_ext= "lzma";

static const char *gzip_cmd = "gunzip ";
static const char *lzo_cmd = "lzop -d ";
static const char *lzma_cmd = "lzma -d ";

// mapping file image
char* image_open(char *fname,struct stat *sb)
{
	int fd;
	char *p;
	
	fd = open(fname,O_RDONLY);
	if(fd == -1){
		printf("error: file open missing...");
		return NULL;
	}
	if(fstat(fd,sb) == -1){
		printf("error: file stat missing...");
		return NULL;		
	}
	if(! S_ISREG(sb->st_mode) ){
		printf("error: %s is not file...",fname);
		return NULL;		
	}
	p = mmap(0,sb->st_size,PROT_READ,MAP_SHARED,fd,0);
	if(p == MAP_FAILED){
		printf("error: mmap missing...");
		return NULL;
	}
	
	if(close(fd) == -1){
		printf("error: close missing...");
		munmap(p,sb->st_size);
		return NULL;
	}
	return p;
}

// find signature into memory image
int check_sign(const char *zimage,off_t msize,off_t *offset,const char* signature,off_t ssize,int passing)
{
	// find a gzip image....
	off_t i,j;
	int matched = 0;
	int pass=0;
	*offset = 0;
	for(i = 0;i < (msize-ssize);i++){
		matched = 0;
		for(j=0;j<ssize;j++){
			if(zimage[i+j] != signature[j]){
				break;
			}
		}
		if(j>=ssize){
			matched = 1;
			pass++;
			if(pass > passing){
				*offset = i;
				return 0;		// OK!
			}
		}
	}
	return 1;		// notfound...
}

// execute shell command
int exec_cmd(char *rcmd,int silent)
{
	FILE *fp;
	int ret;
	char rbuf[512];

	fp = popen(rcmd,"r");
	if(fp == NULL) return -1;
	fflush(fp);
	fread(rbuf,1,254,fp);
	ret = pclose(fp);
	if(silent == 0){
		printf("%s \n returncode %d\n",rcmd,ret);
	}
 	return ret;
}

// write someone image to file
int write_image(char *fname,char *image,size_t length,off_t bountary)
{
	FILE* fp = fopen(fname,"w");
	off_t bb;
	if(fp == NULL){
		printf("error: file open missing...");
		return 1;
	}
	fwrite(image,1,length,fp);
	for(bb=0;bb<bountary;bb++) fputc(0,fp);
	fclose(fp);
	return 0;
}

// decompress zimagefile
int decompress_zimage(char *fname,char *decname)
{
	char *zimage=NULL;
	char tempfn[FILENAME_MAX];
	char execcmd[FILENAME_MAX];
	const char *extname=NULL;
	const char *extcmd=NULL;
	int ret;
	
	off_t z_offset=0;
	struct stat zimage_stat;
	
	zimage = image_open(fname,&zimage_stat);
	if(zimage == NULL){
		goto error_exit;
	}
	
	printf("check zimage file...\n");
	if(check_sign(zimage,zimage_stat.st_size,&z_offset,lzma_signature,13,0) == 0){
		// LZMA
		extname = lzma_ext;
		extcmd = lzma_cmd;
		printf("zimage LZMA compressed.\n");
	}else if(check_sign(zimage,zimage_stat.st_size,&z_offset,lzo_signature,9,1) == 0){
		// LZO
		extname = lzo_ext;
		extcmd = lzo_cmd;
		printf("zimage LZO compressed.\n");
	}else if(check_sign(zimage,zimage_stat.st_size,&z_offset,gzip_signature,3,0) == 0){
		// Gnu ZIP
		extname = gzip_ext;
		extcmd = gzip_cmd;
		printf("zimage gzip compressed.\n");
	}else{
		printf("sorry... unknown zimage.\n");
		goto error_exit;
	}
	
	snprintf(tempfn,FILENAME_MAX,"%s.%s",decname,extname);
	if(write_image(tempfn,zimage+z_offset,zimage_stat.st_size-z_offset,0) == 1){
		goto error_exit;
	}
	snprintf(execcmd,FILENAME_MAX,"%s %s",extcmd,tempfn);
	printf("decompress file...\n");
	ret = exec_cmd(execcmd,0);
	
	if( (ret != 0) && (ret != 512) ){
		printf("decompress missing...\n");
		goto error_exit;
	}
	printf("decompress success.\n");
	munmap(zimage,zimage_stat.st_size);	
	return 0;

error_exit:
	if(zimage != NULL) munmap(zimage,zimage_stat.st_size);
	return 1;

}

// get cpio
int pickup_cpio(char *zimagefn,char*cpiofn)
{
	char *image=NULL;
	char *cpioimage=NULL;
	char found=0;
	
	off_t smark_offset=0,emark_offset=0,cpiosize;
	struct stat image_stat;
	
	image = image_open(zimagefn,&image_stat);
	if(image == NULL){
		goto error_exit;
	}
	
	printf("check cpio image file...\n");
	if(check_sign(image,image_stat.st_size,&smark_offset,cpio_signature,5,0) == 0){
		found = 1;
	}else{
		printf("sorry... unknown image. initramfs not found...\n");
		goto error_exit;
	}
	
	
	cpioimage = image + smark_offset;
	printf("pickup cpio file... \n");
	if(check_sign(cpioimage,image_stat.st_size-smark_offset,&emark_offset,trailer_signature,11,0) != 0){
		printf("sorry... unknown image. initramfs end-marker (trailer) not found...\n");
		goto error_exit;
	}
	printf("found end marker file...\n");
	cpiosize = emark_offset + 11;
	if(write_image(cpiofn,cpioimage,cpiosize,4-(cpiosize%4)) == 1){
		goto error_exit;
	}
	
	munmap(image,image_stat.st_size);	
	return 0;

error_exit:
	if(image != NULL) munmap(image,image_stat.st_size);
	return 1;
}

int main (int argc, char *argv[])
{
	char *decname = "tempdec00";
	char execcmd[FILENAME_MAX];
	
	printf ("initram picker %s\n",VERSTR);
	if(argc < 3){
		printf("initramfs(cpio) taken from kernel zImage \n");
		printf("usage: %s <in_zImage> <outfile> \n",argv[0]);
		goto error_exit;
	}
	
	snprintf(execcmd,FILENAME_MAX,"rm -rf %s*",decname);
	exec_cmd(execcmd,1);
	
	if( decompress_zimage(argv[1],decname) != 0){
		goto error_exit;
	}
	if( pickup_cpio(decname,argv[2]) != 0){
		goto error_exit;
	}
	printf("pickup cpio success.\n");

	snprintf(execcmd,FILENAME_MAX,"rm -rf %s*",decname);
	exec_cmd(execcmd,1);
	
	return 0;	
error_exit:
	printf("exit.\n");
	snprintf(execcmd,FILENAME_MAX,"rm -rf %s*",decname);
	exec_cmd(execcmd,1);
	
	return 1;
}

