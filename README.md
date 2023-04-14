epd_image
---------

A command line tool for preparing bitmap images intended to be displayed on e-paper 2, 3 and 4 color panels. The output is an array of unsigned chars and is written as 1 or 2 separate bit planes depending on the desired output.<br>
<br>
<b>Why did you write it?</b><br>
My existing tool (image_to_c) is similar in that it generates C arrays to compile image data directly into a project. This project is different in that it does color matching and bit plane preparations specifically for e-paper displays. For example, in Black/White/Red EPDs, the black/white pixels are stored on memory plane 0 and the Red pixels are stored on memory plane 1. When you specify --BWR as the output type, the input image's pixels will be matched against B/W/R and 2 memory planes of output will be generated. The image data can full the full resolution of the target e-paper or be an icon/sprite. In the image below, A 96x256 BWR image is drawn in 4 positions.

![EPD_IMAGE](/demo.jpg?raw=true "EPD_IMAGE")

<br>
The code is C99 and can be compiled on any POSIX compliant system (Linux/MacOS/Windows).

If you find this code useful, please consider sending a donation or becoming a Github sponsor.

[![paypal](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=SR4F44J2UR8S4)

