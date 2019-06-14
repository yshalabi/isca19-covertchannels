# ISCA'19 - side-channel tutorial -- hands on session

## Compile

```sh
mkdir build
cd build
cmake ..
make
```


## Flush-Reload covert channel
Make sure the sender & receiver share the same file and file offset
### Run
Sender:
```sh
./demos/fr-send shared.txt offset
```

Receiver:
```sh
./demos/fr-recv shared.txt offset
```
