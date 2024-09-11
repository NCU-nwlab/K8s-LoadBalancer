import time 
import random
import paramiko
from prometheus_api_client import PrometheusConnect
from parameter import url, query_cpu, username, password, ipv4, port, query_forward_packet, query_memory, query_tcp_flag_total, query_layency
from parameter import a,b,c,delta,threshold,unhealthy_threshold,ewma_alpha
class Node:
    def __init__(self, id, load, packet, memory, tcp_flag_rst, latency, name, alpha=ewma_alpha):
        self.id = id
        self.load = load          # cpu load
        self.name = name          # pod name
        self.alpha = alpha        # for EWMA
        self.packet = packet      # packet rate 
        self.memory = memory      # memory usage
        self.tcp_flag_rst = tcp_flag_rst # tcp flag total
        self.latency = latency    # latency
        self.packet_ewma = packet # 記錄新的packet ewma

    def update_packet_rate(self, new_packet_rate):
        self.packet_ewma = self.alpha * new_packet_rate + (1 - self.alpha) * self.packet_ewma # new + old 

class LoadBalancer:
    def __init__(self, nodes):
        self.nodes = nodes

    def select_node(self):
        available_nodes = [node for node in self.nodes if self.is_healthy(node)]

        if len(available_nodes) == 0:
            print("Error: no nodes")
            return -1
        elif len(available_nodes) == 1:
            print("Only one node")
            node1 = available_nodes[0]
            node1.load = 0
            return node1
        else:
            node1, node2 = random.sample(available_nodes, 2)

            total_cpu = node1.load + node2.load
            total_packet = node1.packet_ewma + node2.packet_ewma
            total_memory = node1.memory + node2.memory
            total_latency = node1.latency + node2.latency
            
            node1.cpu = node1.load / total_cpu
            node2.cpu = node2.load / total_cpu
            node1.packet = node1.packet_ewma / total_packet
            node2.packet = node2.packet_ewma / total_packet
            node1.memory = node1.memory / total_memory
            node2.memory = node2.memory / total_memory
            if total_latency != 0:
                node1.latency = node1.latency / total_latency
                node2.latency = node2.latency / total_latency
            
            node1.load = node1.cpu * a + node1.packet * b + node1.memory * c     # 挑整的地方
            node2.load = node2.cpu * a + node2.packet * b + node2.memory * c      # 挑整的地方
            
            if node1.tcp_flag_rst >= threshold:
                node1.load *= delta
                # print("node1 tcp_flag_rst >= threshold")
            else:
                # print("node1 tcp_flag_rst < threshold")
                pass
            if node2.tcp_flag_rst >= threshold:
                node2.load *= delta
                # print("node2 tcp_flag_rst >= threshold")
            else:
                # print("node2 tcp_flag_rst < threshold")
                pass

            if node1.load > node2.load:
                return node2
            else:
                return node1

    def is_healthy(self, node):
        return node.load < unhealthy_threshold


prom = PrometheusConnect(url)
command = "kubectl apply -f cal-route.yaml"

def main():
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(ipv4, port, username, password)

    pod_name = ["wx82t", "mhffj"]
    nodes = [Node(i + 1, 0, 0, 0, 0, 0,name) for i, name in enumerate(pod_name)]
    load_balancer = LoadBalancer(nodes)

    while True:
        result_query_cpu = prom.custom_query(query_cpu)
        result_query_forward_packet = prom.custom_query(query_forward_packet)
        result_query_memory = prom.custom_query(query_memory)
        result_query_tcp_rst = prom.custom_query(query_tcp_flag_total)
        result_query_latency = prom.custom_query(query_layency)

        for i, item in enumerate(result_query_cpu):
            value = float(item['value'][1]) / 20
            load_value = float("{:.2f}".format(value))
            nodes[i].load = load_value
            print(nodes[i].load)

        for j, item in enumerate(result_query_forward_packet):
            value = float(item['value'][1])
            packet_value = float("{:.2f}".format(value))
            nodes[j].packet = packet_value
            nodes[j].update_packet_rate(packet_value)
        
        for k, item in enumerate(result_query_memory):
            value = float(item['value'][1])
            memory_value = float("{:.2f}".format(value))
            nodes[k].memory = memory_value      
            # print(nodes[k].memory)
        
        for l, item in enumerate(result_query_tcp_rst):
            value = float(item['value'][1])
            tcp_flag_value = float("{:.2f}".format(value))
            nodes[l].tcp_flag_total = tcp_flag_value

        for m, item in enumerate(result_query_latency):
            value = float(item['value'][1])
            latency_value = float("{:.2f}".format(value))
            nodes[m].latency = latency_value
        
        selected_node = load_balancer.select_node()

        if selected_node == -1:
            continue

        load = round(100 * (1 - selected_node.load))
        # print(load)
        if selected_node.name == "wx82t":
            command1 = f"sed -i '18s/[0-9][0-9]*/{load}/g' cal-route.yaml"
            command2 = f"sed -i '22s/[0-9][0-9]*/{100-load}/g' cal-route.yaml"
            print(f"high weight pod {load}")
            print(f"low weight pod {100-load}")
        elif selected_node.name == "mhffj":
            command1 = f"sed -i '18s/[0-9][0-9]*/{100-load}/g' cal-route.yaml"
            command2 = f"sed -i '22s/[0-9][0-9]*/{load}/g' cal-route.yaml"
            print(f"high weight pod {load}")
            print(f"low weight pod {100-load}")
        ssh.exec_command(command1)
        ssh.exec_command(command2)
        ssh.exec_command(command)
        time.sleep(3)

    ssh.close()

if __name__ == "__main__":
    main()
