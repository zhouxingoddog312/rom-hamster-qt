/*
 * make-ico —— 把 256×256 PNG 包装成 Windows ICO（构建期工具，不随包发布）。
 *
 * 采用 PNG 压缩帧（Vista 起支持），因此不需要任何图像库：
 *   ICONDIR(6) + ICONDIRENTRY(16) + PNG 原始字节
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char png_signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

static void put_u16le(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
}

static void put_u32le(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static uint32_t read_u32be(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static unsigned char *read_file(const char *path, size_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (data == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_size = (size_t)size;
    return data;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: make-ico <input.png> <output.ico>\n");
        return 2;
    }

    size_t png_size = 0;
    unsigned char *png = read_file(argv[1], &png_size);
    if (png == NULL) {
        fprintf(stderr, "make-ico: cannot read %s\n", argv[1]);
        return 1;
    }

    if (png_size < 33 || memcmp(png, png_signature, sizeof png_signature) != 0) {
        fprintf(stderr, "make-ico: %s is not a PNG file\n", argv[1]);
        free(png);
        return 1;
    }

    /* IHDR 紧随签名：长度(4) + "IHDR"(4) + width(4) + height(4) */
    uint32_t width = read_u32be(png + 16);
    uint32_t height = read_u32be(png + 20);

    unsigned char header[22];
    memset(header, 0, sizeof header);
    put_u16le(header + 2, 1);                       /* type: icon */
    put_u16le(header + 4, 1);                       /* image count */
    header[6] = (unsigned char)(width >= 256 ? 0 : width);
    header[7] = (unsigned char)(height >= 256 ? 0 : height);
    put_u16le(header + 10, 1);                      /* color planes */
    put_u16le(header + 12, 32);                     /* bits per pixel */
    put_u32le(header + 14, (uint32_t)png_size);     /* bytes in resource */
    put_u32le(header + 18, (uint32_t)sizeof header);/* image offset */

    FILE *out = fopen(argv[2], "wb");
    if (out == NULL) {
        fprintf(stderr, "make-ico: cannot write %s\n", argv[2]);
        free(png);
        return 1;
    }

    int ok = fwrite(header, 1, sizeof header, out) == sizeof header &&
             fwrite(png, 1, png_size, out) == png_size;
    fclose(out);
    free(png);

    if (!ok) {
        fprintf(stderr, "make-ico: write failed\n");
        remove(argv[2]);
        return 1;
    }
    return 0;
}
