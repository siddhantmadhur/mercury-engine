#include "images.hh"
#include <iostream>


Image::Image(std::string pathname) {
    raw_data = stbi_load(
        pathname.c_str(), &x, &y, &channels, 4)  ;
    if (raw_data) {
        std::cout << "Loaded image!" << std::endl;
    } else {
        std::cerr << "Could not load image!" << std::endl;
    }
}
