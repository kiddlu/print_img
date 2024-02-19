all: pimg

pimg: main.cpp print_img.cpp
	g++ -o $@ $^ -lm -lpthread

clean:
	rm pimg
