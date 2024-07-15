import sys

if len(sys.argv) == 1:
	print("Usage:")
	print("\t", sys.argv[0], " filename")
	exit(-1)
else:
	fname = sys.argv[1]

params = []
values = []

fin = open(fname, 'r')
line = fin.readline()
while len(line) > 0:
	params.append(int(line))
	line = fin.readline()
	if "IOPS: " in line:
		line = line.replace("IOPS: ", "")
		values.append(float(line.strip('Kops/s\n')))
	else :
		print("invalid file contend")
		exit(-1)
	line = fin.readline()
fin.close()

for p, v in zip(params, values):
	print(v)