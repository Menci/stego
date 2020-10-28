# 数据隐写
将任意数据嵌入到 BMP 文件中，使得观察出与原图的区别。生成的 BMP 文件可以被转码（不可有损压缩）。

```bash
# 编译
gcc stego.c -o stego
# 写入
./stego -w input.bmp hello.7z output.bmp
# 读取
./stego -r output.bmp hello.7z
```

**原理**：利用 BMP 中每个像素点 RGB 颜色值的最低位（LSB）来存放数据。
