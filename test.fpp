extern printd(x);
extern sleep_s(x);
extern clear();
extern fabs(x);
extern fmod(x y);
extern pow(x y);
extern floor(x);
extern sin(x);

extern rectangle0(px py sx sy);
extern rectangle1(px py sx sy color angle);

extern sprite0(string path px py sx sy);


# Logical unary not.
def unary!(v)
  if v then
    0
  else
    1

# Unary negate.
def unary-(v)
  0-v

# Binary logical or, which does not short circuit.
def binary| 5 (LHS RHS)
  if LHS then
    1
  else if RHS then
    1
  else
    0

def bin_a(x y)
  if !x then
    0
  else
    !!y

def f0_60(x) bin_a(x > -1, x < 60)
def f60_120(x) bin_a(x > 59, x < 120)
def f120_180(x) bin_a(x > 119, x < 180)
def f180_240(x) bin_a(x > 179, x < 240)
def f240_300(x) bin_a(x > 239, x < 300)

def clamp_255(x)
  if x > 255 then
    255
  else
    x

def to_32B(R G B)
  (clamp_255(floor(R)) * pow(256, 3)) + 
  (clamp_255(floor(G)) * pow(256, 2)) +  
  (clamp_255(floor(B)) * 256) + 255

def hue_to_rgb32B(H)
  var s = 1.0 in
  var v = 1.0 in
  var C = s * v in
  var X = C * (1 - fabs(fmod(H / 60.0, 2) - 1)) in
  var m = v - C in
  if f0_60(H) then to_32B((C + m) * 255, (X + m) * 255, (0 + m) * 255)
  else if f60_120(H) then to_32B((X + m) * 255, (C + m) * 255, (0 + m) * 255)
  else if f120_180(H) then to_32B((0 + m) * 255, (C + m) * 255, (X + m) * 255)
  else if f180_240(H) then to_32B((0 + m) * 255, (X + m) * 255, (C + m) * 255)
  else if f240_300(H) then to_32B((X + m) * 255, (0 + m) * 255, (C + m) * 255)
  else to_32B((C + m) * 255, (0 + m) * 255, (X + m) * 255)

def animate_grid(px py sx sy steps delay rows cols) {
  for i = 0, i < steps, 1.0 in {
    clear()
    for r = 0, r < rows, 1.0 in {
      for c = 0, c < cols, 1.0 in {
        var layer_offset = i * 0.01 in
        var color = hue_to_rgb32B(((360 / steps) * i + layer_offset) % 360) in
        var angle = 0 in  # Radians
        var rect_size = sx + 5 * sin(i * 0.1 + c) in
        rectangle1(px + c * (rect_size + 10) + layer_offset, py + r * (rect_size + 10) + layer_offset, rect_size, rect_size, color, angle)
      }
    }
    sleep_s(0.01)
  }
}

def main() {
	for i = 0, i < 10, 1.0 in {
    sprite0("images/duck.webp", 400, 400, 400, 400)
		animate_grid(100, 200, 15, 15, 500, 0.05, 10, 10)		
		clear()
	}
}
main();




