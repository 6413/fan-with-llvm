extern model3d(string path px py pz scale);
extern sprite0(string path px py sx sy);

def main() {
	model3d("models/cube.fbx", 0.3, 0, 0, 0.01);
	sprite0("images/duck.webp", 300, 300, 200, 200);
}