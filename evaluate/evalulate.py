from PIL import Image, ImageOps
import numpy as np
import cv2
from skimage.metrics import structural_similarity as compare_ssim
import os

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

def print_l2(a, b) :
	diff = np.abs(a - b)
	l2 = np.linalg.norm(diff, axis=(0,1))
	#print(l2.shape)
	#print(type(l2))
	print(l2)


grayscale = True
folder = "../captures/1/"


gt_filename = folder + "/gt.png"
bad_filename = folder + "/bad.png"
good_filename = folder + "/good.png"


# ---------------------------
# LOAD IMAGES
# ---------------------------

ground_truth = load_linear(gt_filename, grayscale)
bad = load_linear(bad_filename, grayscale)
good = load_linear(good_filename, grayscale)



# ---------------------------
# SIMPLE L2-NORM
# ---------------------------

print("L2 (lower is better):")
print_l2(ground_truth, bad)
print_l2(ground_truth, good)
print("----")



# ---------------------------
# L2-NORM OF FREQUENCY IMAGES
# ---------------------------

def fft(arr) :
	f = np.fft.fft2(arr)
	fshift = np.fft.fftshift(f)
	magnitude_spectrum = 20*np.log(np.abs(fshift))
	return magnitude_spectrum

gt_fft = fft(ground_truth)
bad_fft = fft(bad)
good_fft = fft(good)

#save_srgb(gt_fft, folder + "/ground_truth_fft.png")

print("FFT L2 (lower is better):")
print_l2(gt_fft, bad_fft)
print_l2(gt_fft, good_fft)
print("----")

#diff[:,:,3] = 1.0
#save_srgb(diff, "captures/diff.png")



# ---------------------------
# SSIM
# ---------------------------

def print_ssim(a, b, visualize_name=None) :
	score, diff = compare_ssim(a, b, full=True)
	print(score)

	#if visualize_name != None :
	#	diff = (diff * 255).astype("uint8")
	#	thresh = cv2.threshold(diff, 0, 255, cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)[1]
	#	cv2.imshow(visualize_name, thresh)


print("SSIM (higher is better):")
print_ssim(ground_truth, bad, "SSIM Bad")	
print_ssim(ground_truth, good, "SSIM Good")
cv2.waitKey(1)
print("----")



# ---------------------------
# PSNR
# ---------------------------

def print_psnr(a, b) :
	print(cv2.PSNR(a, b))

print("PSNR (higher is better):")
print_psnr(ground_truth, bad)	
print_psnr(ground_truth, good)	
print("----")





# ---------------------------
# VGG 19
# ---------------------------

def vgg19() :
	import torch as t
	import torchvision as tv
	import torch.nn as nn

	import sys

	def from_device(tensor):
		return tensor.detach().cpu().numpy()

	class data_set(t.utils.data.Dataset):

		def __init__(self, transforms=None):
			self.transforms = transforms
			self.files = [ gt_filename, bad_filename, good_filename ]
			
		def __len__(self):
			return len(self.files)

		def __getitem__(self, idx):
			sample = Image.open(self.files[idx]).convert('RGB')
			if self.transforms:
				sample = self.transforms(sample)
			return sample

	transforms = tv.transforms.Compose([tv.transforms.Resize((224, 224)), tv.transforms.ToTensor()])
	data = data_set(transforms=transforms)

	dataloader = t.utils.data.DataLoader(data)

	device = "cpu"

	extractor = tv.models.vgg19(pretrained=True).to(device) # Load the model
	extractor.classifier = nn.Sequential(*list(extractor.classifier.children())[:5]) # VGG19 fc1
	extractor.eval() # Set model to evaluation mode

	feature_size = 4096 # VGG19 fc1
	features = np.zeros((len(data), feature_size))

	for i, inputs in enumerate(dataloader):
		outputs = from_device(extractor(inputs.to(device))) # Push image through network
		features[i*len(inputs):i*len(inputs)+len(inputs)] = outputs

	np.set_printoptions(threshold=sys.maxsize)

	gt_features = features[0]
	bad_features = features[1]
	good_features = features[2]

	print(np.linalg.norm(np.abs(gt_features - bad_features)))
	print(np.linalg.norm(np.abs(gt_features - good_features)))


print("VGG 19 (lower is better):")
vgg19()
print("----")






# ---------------------------
# PATTERN MATCHING
# ---------------------------

def pattern_match(a, patch_size, compare, visualize_name=None) :
	w = a.shape[1]
	h = a.shape[0]

	patches_x = w // patch_size
	patches_y = h // patch_size

	method = cv2.TM_SQDIFF
	p2 = patch_size / 2.0
	p2 = np.array((p2, p2))

	sum = 0

	if visualize_name != None:
		vis = np.zeros(a.shape, dtype=np.uint8)

	for y in range(patches_y) :
		for x in range(patches_x) :
			start_y = y * patch_size
			start_x = x * patch_size
			patch = a[start_y : start_y + patch_size, start_x : start_x + patch_size]
			res = cv2.matchTemplate(compare, patch, method)
			
			min_val, max_val, min_loc, max_loc = cv2.minMaxLoc(res)
			sum += min_val

			top_left = min_loc
			bottom_right = (top_left[0] + w, top_left[1] + h)

			start = np.array((start_x, start_y)) + p2
			end = np.array((min_loc[0], min_loc[1])) + p2

			dist = start - end
			dist = np.sqrt(dist[0] * dist[0] + dist[1] * dist[1])

			if visualize_name != None and dist < patch_size:
				#print(start, type(start))
				#print(end, type(end))
				#print(dist)
				cv2.line(vis, (int(start[0]), int(start[1])), (int(end[0]), int(end[1])), 255)

	#if visualize_name != None:
	#	cv2.imshow(visualize_name, vis)

	return sum


print("Pattern match (lower is better):")
print(pattern_match(ground_truth, 20, bad, "Match bad"))
print(pattern_match(ground_truth, 20, good, "Match good"))
print("----")


cv2.waitKey(0)


