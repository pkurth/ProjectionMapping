from PIL import Image, ImageOps
import numpy as np


def load_linear(path, grayscale=False) :
	img = Image.open(path)
	if grayscale :
		img = ImageOps.grayscale(img)

	arr = (np.float32(np.array(img)) / 255.0) ** 2.2
	return arr
	
def save_srgb(arr, path) :
	a = np.clip(arr, 0.0, 1.0)
	a = a ** (1.0 / 2.2)
	a = a * 255.0

	Image.fromarray(np.uint8(a)).save(path)

def fft(arr) :
	f = np.fft.fft2(arr)
	fshift = np.fft.fftshift(f)
	magnitude_spectrum = 20*np.log(np.abs(fshift))
	return magnitude_spectrum


def print_l2(a, b) :
	diff = np.abs(a - b)
	l2 = np.linalg.norm(diff, axis=(0,1))
	#print(l2.shape)
	#print(type(l2))
	print(l2)


grayscale = True

folder = "captures/cropped"

ground_truth = load_linear(folder + "/gt.png", grayscale)
bad = load_linear(folder + "/bad.png", grayscale)
good = load_linear(folder + "/good.png", grayscale)

print_l2(ground_truth, bad)
print_l2(ground_truth, good)


gt_fft = fft(ground_truth)
bad_fft = fft(bad)
good_fft = fft(good)

save_srgb(gt_fft, "captures/ground_truth_fft.png")

print_l2(gt_fft, bad_fft)
print_l2(gt_fft, good_fft)

#diff[:,:,3] = 1.0
#save_srgb(diff, "captures/diff.png")


