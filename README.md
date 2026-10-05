*One year of learning in 10 minutes.*

Rules:
- No AI
- One file
- Zero dependencies
- Works everywhere
- Somewhat fast
- Under 500 lines of C99 code

This is a ray tracer written in one file of C99 code with *zero* external dependencies.

To compile:
```
cc -std=c99 -O2 -Wall -Wextra -pedantic -o main main.c -lm
```

To generate the image:
```
./main > image.ppm
```

Part 1 - Output circle to ppm
1. Generate pure red ppm image 
2. Vector utils
3. Ray utilities
4. Sphere intersection function
5. Camera

Part 2:
1. Surface Normals
2. Antialiasing
3. Triangle
4. STL file support
5. Material Tables - Lambertian, Metals, Dielectrics
6. Lights

Part 3 - Performance:
1. Russian Roulette
2. BVH
3. Instancing

No AI was used to write any of the code in this repo.
