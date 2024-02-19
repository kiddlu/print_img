all: pimg

pimg: main.cpp print_img.cpp
	g++ -o $@ $^ -L./ -lm -lpthread

clean:
	rm pimg
